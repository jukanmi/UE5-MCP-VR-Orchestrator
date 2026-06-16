@echo off
REM TTSService (FastAPI 8001) - CosyVoice2-0.5B @ WSL2(Ubuntu).
REM 코드/에셋은 /mnt/c 리포, 런타임(venv+repo+모델)은 WSL 홈(~/).
REM WSL2 localhost 포워딩으로 Windows 의 CognitiveEngine/UE5 가 127.0.0.1:8001 직통.
REM LD_LIBRARY_PATH = torch 번들 nvidia libs (cuDNN9 등) — onnxruntime-gpu CUDA provider 로드용.
REM   미설정 시 flow.decoder onnx 가 CPU 폴백 → RTF 급증(병목).
wsl bash -c "cd /mnt/c/github/UE5_MCP_VR/OmniAgent_VR_System && source ~/cosyvoice-venv/bin/activate && SITE=$(python -c 'import site;print(site.getsitepackages()[0])') && export LD_LIBRARY_PATH=$SITE/nvidia/cudnn/lib:$SITE/nvidia/cublas/lib:$SITE/nvidia/cuda_runtime/lib:$SITE/nvidia/cuda_nvrtc/lib:$SITE/nvidia/cufft/lib:$SITE/nvidia/curand/lib:$SITE/nvidia/cusparse/lib:$SITE/nvidia/nccl/lib && COSYVOICE_REPO=$HOME/CosyVoice COSYVOICE_MODEL_DIR=$HOME/models/CosyVoice2-0.5B PYTHONPATH=$HOME/CosyVoice:$HOME/CosyVoice/third_party/Matcha-TTS python -m uvicorn TTSService.server:app --host 0.0.0.0 --port 8001"
pause
