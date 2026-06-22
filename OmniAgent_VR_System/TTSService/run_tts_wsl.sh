#!/bin/bash
# TTSService (CosyVoice2-0.5B) WSL2 런처.
# WHY 별도 스크립트: cmd.exe BAT 의 `start ... wsl bash -c "..."` 인라인은 명령에 포함된
#   괄호 $()·getsitepackages()·작은따옴표를 mangle 해 SITE 가 빈 값이 됨 → LD_LIBRARY_PATH
#   깨짐 → onnxruntime CUDA provider 가 libcudnn_adv.so.9 못 찾아 CPU 폴백(RTF 급증).
#   모든 fragile 한 bash 문법을 이 파일 안에 가둬 cmd 파싱 노출을 제거한다.
# LD_LIBRARY_PATH: torch 번들 nvidia libs(cuDNN9 등) — onnxruntime-gpu CUDA provider 로드용.
set -e

# 스크립트 위치 기준 OmniAgent_VR_System 으로 이동(uvicorn 모듈 경로 TTSService.server).
cd "$(dirname "$0")/.." || exit 1

source ~/cosyvoice-venv/bin/activate

SITE=$(python -c 'import site;print(site.getsitepackages()[0])')
export LD_LIBRARY_PATH=$SITE/nvidia/cudnn/lib:$SITE/nvidia/cublas/lib:$SITE/nvidia/cuda_runtime/lib:$SITE/nvidia/cuda_nvrtc/lib:$SITE/nvidia/cufft/lib:$SITE/nvidia/curand/lib:$SITE/nvidia/cusparse/lib:$SITE/nvidia/nccl/lib

export COSYVOICE_REPO=$HOME/CosyVoice
export COSYVOICE_MODEL_DIR=$HOME/models/CosyVoice2-0.5B
export PYTHONPATH=$HOME/CosyVoice:$HOME/CosyVoice/third_party/Matcha-TTS

exec python -m uvicorn TTSService.server:app --host 0.0.0.0 --port 8001
