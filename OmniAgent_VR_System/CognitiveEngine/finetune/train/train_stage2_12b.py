r"""Stage2 12B plan QLoRA 학습 — unsloth.

Blackwell 안정화·mojibake/pickle 회피는 train_stage1_e4b.py 와 동일 이식(2026-07-21 검증).

실행 (chatbot venv):
    C:\github\chatbot\venv\Scripts\python.exe train/train_stage2_12b.py \
        --model unsloth/gemma-4-12B-it \
        --data data/processed/stage2_12b_train.jsonl \
        --output train/outputs/stage2_12b

⚠️ 검열 회귀 스팟체크(censorship_check.py 게이트) — 현 배포본은 abliterated Q4_K_M.
학습 후 전투/폭력 held-out 세트로 거부·순화 응답 확인 필수(eval/censorship_check.py 별도).
"""
import argparse
import json
import os
import sys
import tempfile

os.environ.setdefault("CUDA_LAUNCH_BLOCKING", "1")
os.environ.setdefault("PYTORCH_CUDA_ALLOC_CONF", "expandable_segments:True")
os.environ.setdefault("TORCHDYNAMO_DISABLE", "1")  # Blackwell AOT backward cudaErrorUnknown 회피

import torch

import pickle as _stdlib_pickle
from datasets import load_dataset
import datasets.utils._dill as _datasets_dill

if sys.version_info >= (3, 13):
    def _py314_batch_setitems(self, items, obj=None):
        if obj is None:
            items_list = list(items)
            _stdlib_pickle._Pickler._batch_setitems(self, iter(items_list), dict(items_list))
        else:
            _stdlib_pickle._Pickler._batch_setitems(self, items, obj)

    _datasets_dill.Pickler._batch_setitems = _py314_batch_setitems
    import dill._dill as _dill_internals
    _dill_internals.Pickler._batch_setitems = _py314_batch_setitems

from unsloth import FastLanguageModel
from unsloth.chat_templates import train_on_responses_only
from transformers import TrainingArguments
from trl import SFTTrainer

ap = argparse.ArgumentParser()
ap.add_argument("--model", default="unsloth/gemma-4-12B-it")
ap.add_argument("--data", default="data/processed/stage2_12b_train.jsonl")
ap.add_argument("--output", default="train/outputs/stage2_12b")
ap.add_argument("--epochs", type=int, default=2)
ap.add_argument("--resume", action="store_true")
args = ap.parse_args()

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = args.data if os.path.isabs(args.data) else os.path.join(HERE, args.data)
OUT = args.output if os.path.isabs(args.output) else os.path.join(HERE, args.output)

MAX_SEQ_LENGTH = 1024  # Blackwell+WSL2 seq>1024 크래시 실측(chatbot/e4b 공통)
LORA_R = 16            # e4b(32)보다 낮음, 12B 4bit VRAM 여유 고려
LORA_ALPHA = 16
TARGET_MODULES = ["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"]
LEARNING_RATE = 1.0e-4
BATCH_SIZE = 1
GRAD_ACCUM = 16
WARMUP_RATIO = 0.05

print(f"[1/4] 모델 로드: {args.model}")
model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=args.model,
    max_seq_length=MAX_SEQ_LENGTH,
    dtype=None,
    load_in_4bit=True,
)
model = FastLanguageModel.get_peft_model(
    model,
    r=LORA_R,
    lora_alpha=LORA_ALPHA,
    target_modules=TARGET_MODULES,
    lora_dropout=0,
    bias="none",
    use_gradient_checkpointing=True,
    random_state=3407,
)

import torch.func as _torch_func

def _safe_grad_and_value(func, argnums=0, has_aux=False):
    def wrapper(*a, **kw):
        argnums_tuple = (argnums,) if isinstance(argnums, int) else tuple(argnums)
        a_list = list(a)
        inputs_for_grad = []
        for i in argnums_tuple:
            t = a_list[i].detach().requires_grad_(True)
            a_list[i] = t
            inputs_for_grad.append(t)
        torch.cuda.synchronize()
        with torch.enable_grad():
            result = func(*a_list, **kw)
            torch.cuda.synchronize()
            (value, aux) = result if has_aux else (result, None)
            grads = torch.autograd.grad(value, inputs_for_grad, allow_unused=True)
        torch.cuda.synchronize()
        grads = tuple(g if g is not None else torch.zeros_like(inp)
                      for g, inp in zip(grads, inputs_for_grad))
        if isinstance(argnums, int):
            grads = grads[0]
        return (grads, (value, aux)) if has_aux else (grads, value)
    return wrapper

_torch_func.grad_and_value = _safe_grad_and_value

import torch.autograd as _tautograd
_orig_backward = _tautograd.backward

def _sync_then_backward(tensors, grad_tensors=None, retain_graph=None,
                        create_graph=False, grad_variables=None, inputs=None):
    torch.cuda.synchronize()
    return _orig_backward(tensors, grad_tensors, retain_graph, create_graph, grad_variables, inputs)

_tautograd.backward = _sync_then_backward

import transformers.trainer as _hf_trainer

def _save_model_only(self, output_dir=None, state_dict=None):
    output_dir = output_dir or self.args.output_dir
    os.makedirs(output_dir, exist_ok=True)
    self.model.save_pretrained(output_dir)
    pc = getattr(self, "processing_class", None) or getattr(self, "tokenizer", None)
    if pc is not None:
        pc.save_pretrained(output_dir)
    print(f"    [checkpoint] adapter 저장 → {output_dir}")

_hf_trainer.Trainer._save = _save_model_only
print("    Blackwell 안정화 패치 적용됨(e4b 동형)")

print(f"[2/4] 데이터 로드: {DATA}")
raw = [json.loads(l) for l in open(DATA, encoding="utf-8")]

def to_text(sample: dict) -> str:
    return tokenizer.apply_chat_template(sample["messages"], tokenize=False, add_generation_prompt=False)

_tmp = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False, encoding="utf-8")
for s in raw:
    _tmp.write(json.dumps({"text": to_text(s)}, ensure_ascii=False) + "\n")
_tmp.close()
dataset = load_dataset("json", data_files=_tmp.name, split="train")
os.unlink(_tmp.name)
print(f"    샘플 수: {len(dataset)}")

print("[3/4] 학습 시작")
trainer = SFTTrainer(
    model=model,
    tokenizer=tokenizer,
    train_dataset=dataset,
    dataset_text_field="text",
    max_seq_length=MAX_SEQ_LENGTH,
    dataset_num_proc=1,
    packing=False,
    args=TrainingArguments(
        per_device_train_batch_size=BATCH_SIZE,
        gradient_accumulation_steps=GRAD_ACCUM,
        warmup_ratio=WARMUP_RATIO,
        num_train_epochs=args.epochs,
        learning_rate=LEARNING_RATE,
        fp16=not torch.cuda.is_bf16_supported(),
        bf16=torch.cuda.is_bf16_supported(),
        logging_steps=5,
        optim="adamw_8bit",
        weight_decay=0.01,
        lr_scheduler_type="cosine",
        seed=3407,
        output_dir=OUT,
        report_to="none",
        save_strategy="steps",
        save_steps=10,
        save_total_limit=3,
        save_only_model=True,
    ),
)

# gemma-4 마커 — probe_template.py 실측(2026-07-21): <|turn>role\n...<turn|> (gemma-3 아님).
trainer = train_on_responses_only(
    trainer,
    instruction_part="<|turn>user\n",
    response_part="<|turn>model\n",
)

trainer.train(resume_from_checkpoint=args.resume or None)

adapter_dir = os.path.join(OUT, "adapter")
model.save_pretrained(adapter_dir)
tokenizer.save_pretrained(adapter_dir)
print(f"[4/4] adapter 저장 → {adapter_dir}")
