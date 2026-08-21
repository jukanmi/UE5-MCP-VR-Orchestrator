#!/usr/bin/env python
"""LoRA adapter → merge → GGUF Q4_K_M (unsloth save_pretrained_gguf).

chatbot/export_gguf.py 검증 방식 — merge+gguf+quant 한 번에(llama.cpp 자동, RAM 오프로딩).

    # Stage1 e4b (기본값)
    C:\\github\\chatbot\\venv\\Scripts\\python.exe train/merge_and_export.py
    # Stage2 12B plan
    C:\\github\\chatbot\\venv\\Scripts\\python.exe train/merge_and_export.py \\
        --adapter train/outputs/stage2_12b/adapter --out deploy/out_stage2
"""

import argparse
import os

os.environ.setdefault("TORCHDYNAMO_DISABLE", "1")

from unsloth import FastLanguageModel

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # finetune/

# Stage2 도 같은 경로를 타야 하므로 어댑터·출력 경로를 인자화(기본값은 Stage1 = 기존 동작).
# 출력 디렉터리를 스테이지마다 분리하지 않으면 e4b GGUF 를 12B 산출물이 덮어쓴다.
ap = argparse.ArgumentParser()
ap.add_argument("--adapter", default="train/outputs/stage1_e4b/adapter")
ap.add_argument("--out", default="deploy/out")
args = ap.parse_args()

ADAPTER = args.adapter if os.path.isabs(args.adapter) else os.path.join(HERE, args.adapter)
OUT = args.out if os.path.isabs(args.out) else os.path.join(HERE, args.out)
os.makedirs(OUT, exist_ok=True)

print(f"[1/2] adapter 로드: {ADAPTER}")
model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=ADAPTER,
    max_seq_length=1024,
    dtype=None,
    load_in_4bit=True,
)

print(f"[2/2] GGUF Q4_K_M 저장: {OUT}")
model.save_pretrained_gguf(OUT, tokenizer, quantization_method="q4_k_m")
print("[완료] GGUF 추출 성공")
