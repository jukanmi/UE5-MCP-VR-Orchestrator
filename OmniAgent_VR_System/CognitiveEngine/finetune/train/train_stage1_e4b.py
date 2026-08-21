r"""Stage1 e4b QLoRA 학습 (SPEC_finetune §3.2) — unsloth.

⚠️ Blackwell(RTX 5070 Ti SM120) 안정화는 C:\github\chatbot\train.py 실전 검증본에서 이식.
   해당 패치 없이는 step 4~5 에서 "CUDA driver error: device not ready" 크래시.

실행 (chatbot venv 재사용 — torch2.11+cu128 sm_120 + unsloth2026.4.4):
    C:\github\chatbot\venv\Scripts\python.exe train/train_stage1_e4b.py \
        --model google/gemma-4-E4B-it \
        --data data/processed/stage1_e4b_train.jsonl \
        --output train/outputs/stage1_e4b

데이터: {"messages":[system,user,assistant]} — assistant(DialogueResponse JSON)에만 loss.
"""

import argparse
import json
import os
import sys
import tempfile

# ── Blackwell(SM120) 스트림 안정화 — torch import 전 필수 (chatbot/train.py 이식) ──
# bitsandbytes 비동기 dequantize 스트림 ↔ Triton fused CE loss 스트림 충돌 → step4 크래시.
os.environ.setdefault("CUDA_LAUNCH_BLOCKING", "1")
os.environ.setdefault("PYTORCH_CUDA_ALLOC_CONF", "expandable_segments:True")
# Blackwell+Windows: torch-compile/AOT autograd backward 의 memory_format coerce 에서
# 간헐적 "CUDA error: unknown error"(cudaErrorUnknown) 크래시(175스텝서 발생, 2026-07-20).
# Dynamo/AOT 비활성 → eager backward 로 그 경로 자체 우회. 부수적으로 unsloth 의
# SFTConfig 몽키패치도 줄어 체크포인트 torch.save pickle identity 문제도 완화.
os.environ.setdefault("TORCHDYNAMO_DISABLE", "1")

import torch

# Python 3.14 pickle 시그니처 변경 → datasets/dill _batch_setitems 2-arg TypeError 우회.
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

# ── 인자 ──
ap = argparse.ArgumentParser()
ap.add_argument("--model", default="google/gemma-4-E4B-it", help="베이스 모델 ID")
ap.add_argument("--data", default="data/processed/stage1_e4b_train.jsonl")
ap.add_argument("--output", default="train/outputs/stage1_e4b")
ap.add_argument("--epochs", type=int, default=3)
ap.add_argument("--resume", action="store_true")
args = ap.parse_args()

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # finetune/
DATA = args.data if os.path.isabs(args.data) else os.path.join(HERE, args.data)
OUT = args.output if os.path.isabs(args.output) else os.path.join(HERE, args.output)

# Blackwell+WSL2 에서 seq>1024 는 Triton "device not ready" 크래시 — 1024 만 완주 확인(chatbot).
MAX_SEQ_LENGTH = 1024
LORA_R = 32  # SPEC §3.2 (chatbot 은 16, gemma e4b 4bit 여유 있어 32 채택)
LORA_ALPHA = 32
TARGET_MODULES = ["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"]
LEARNING_RATE = 2.0e-4
# batch 1×accum 4 — 유효배치 4·총 스텝수는 2×2 와 동일하되 활성값 메모리가 절반.
# 근거(2026-07-30 실측): 페르소나 풀 300장 도입 후 시퀀스가 p50 943·p90 981 로 1024 캡에
# 거의 붙어, batch 2 면 매 스텝이 활성값 최대치 → VRAM 16GB 포화(여유 175MiB)로
# WDDM 이 시스템 RAM 으로 페이징(OOM 이 아니라 조용한 감속). step 시간이 6s→39s 로
# 단조 증가하다 사실상 정지(07-24 런은 3863s/it). 열·전력 스로틀은 무관(41°C, Not Active).
BATCH_SIZE = 1
GRAD_ACCUM = 4
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
    use_gradient_checkpointing=True,  # Native GC — Blackwell 안정성(chatbot)
    random_state=3407,
)

# ── Blackwell+bitsandbytes 스트림 충돌 런타임 패치 (chatbot/train.py 이식) ──
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
        grads = tuple(g if g is not None else torch.zeros_like(inp) for g, inp in zip(grads, inputs_for_grad))
        if isinstance(argnums, int):
            grads = grads[0]
        return (grads, (value, aux)) if has_aux else (grads, value)

    return wrapper


_torch_func.grad_and_value = _safe_grad_and_value

import torch.autograd as _tautograd

_orig_backward = _tautograd.backward


def _sync_then_backward(
    tensors, grad_tensors=None, retain_graph=None, create_graph=False, grad_variables=None, inputs=None
):
    torch.cuda.synchronize()
    return _orig_backward(tensors, grad_tensors, retain_graph, create_graph, grad_variables, inputs)


_tautograd.backward = _sync_then_backward
print("    Blackwell 스트림 동기화 패치 적용됨")

# 체크포인트 저장 시 torch.save(SFTConfig) → "Can't pickle: not the same object" 크래시 회피.
# unsloth 2026.7.3 이 SFTConfig 를 런타임 재정의해 클래스 identity 가 깨짐.
# _save 를 어댑터+토크나이저만 저장하도록 교체(training_args.bin pickle 스킵).
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
print("    체크포인트 pickle 회피 패치 적용됨")

# ── 데이터: {"messages":[...]} → chat template 텍스트 (JSONL 우회 로드) ──
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

# ── 학습 ──
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
        logging_steps=10,
        optim="adamw_8bit",  # paged 는 WSL2 페이징 오류(chatbot)
        weight_decay=0.01,
        lr_scheduler_type="cosine",
        seed=3407,
        output_dir=OUT,
        report_to="none",
        # 20스텝마다 저장 — 간헐 CUDA 크래시 대비 resume 지점 확보(save off 로 175스텝 소실했음).
        # save_only_model: optimizer/rng 스킵으로 저장 부담·pickle 표면 축소.
        save_strategy="steps",
        save_steps=20,
        save_total_limit=3,
        save_only_model=True,
    ),
)

# gemma-4 마커 — template 은 <|turn>role\n...<turn|> 형식(gemma-3 <start_of_turn> 아님).
# assistant(<|turn>model)에만 loss.
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
