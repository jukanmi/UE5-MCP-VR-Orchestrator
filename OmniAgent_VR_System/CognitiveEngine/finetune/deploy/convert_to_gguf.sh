#!/usr/bin/env bash
# merged HF → GGUF f16 → Q4_K_M → Ollama 태그 (SPEC_finetune §3.2 배포).
# 선행: llama.cpp 클론+빌드(convert_hf_to_gguf.py, llama-quantize). LLAMA_CPP 로 경로 지정.
#   git clone https://github.com/ggml-org/llama.cpp && cd llama.cpp && cmake -B build && cmake --build build -j
# 실행: bash deploy/convert_to_gguf.sh <merged_dir> <ollama_tag> [base_tag]
#   base_tag = TEMPLATE/PARAMETER 를 상속할 기존 ollama 태그. 기본 gemma4:e4b(Stage1).
#   Stage2 12B plan 은 반드시 gemma4-12b 를 넘길 것 — e4b 파라미터를 물려받으면
#   12B 서빙 설정이 e4b 것으로 덮여 조용히 어긋난다.
#   예: bash deploy/convert_to_gguf.sh out_stage2 gemma4-12b-plan-v1 gemma4-12b
set -e
MERGED="${1:?merged_dir 인자 필요}"
TAG="${2:?ollama_tag 인자 필요 (예: gemma4-e4b-dialogue-v1)}"
BASE_TAG="${3:-gemma4:e4b}"
LLAMA_CPP="${LLAMA_CPP:-$HOME/llama.cpp}"
HERE="$(cd "$(dirname "$0")" && pwd)"
# 출력도 태그별로 분리 — 공용 out/ 이면 스테이지 간 gguf 가 서로를 덮어쓴다.
OUT="$HERE/out_$TAG"; mkdir -p "$OUT"

echo "[gguf] f16 변환"
python "$LLAMA_CPP/convert_hf_to_gguf.py" "$MERGED" --outfile "$OUT/model-f16.gguf" --outtype f16

echo "[gguf] Q4_K_M 양자화"
"$LLAMA_CPP/build/bin/llama-quantize" "$OUT/model-f16.gguf" "$OUT/model-Q4_K_M.gguf" Q4_K_M

# Modelfile: 서빙 파라미터 드리프트 방지 — 기존 태그 TEMPLATE/PARAMETER 상속.
# ollama show <base_tag> --modelfile 에서 FROM 만 신규 gguf 로 치환.
echo "[gguf] Modelfile 생성 ($BASE_TAG 파라미터 상속)"
{
  echo "FROM $OUT/model-Q4_K_M.gguf"
  ollama show "$BASE_TAG" --modelfile | grep -vE '^FROM |^#'
} > "$OUT/Modelfile"

echo "[gguf] ollama create $TAG"
ollama create "$TAG" -f "$OUT/Modelfile"
echo "[gguf] 완료 — llm_factory MODELS 를 '$TAG' 로 스위치하세요"
echo "        (Stage1=MODELS['gemma4_slm'] · Stage2 plan=MODELS['gemma4'])."
