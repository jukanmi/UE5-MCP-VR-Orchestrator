@echo off
REM TTSService (FastAPI 8001) - CosyVoice2-0.5B @ WSL2(Ubuntu).
REM 코드/에셋은 /mnt/c 리포, 런타임(venv+repo+모델)은 WSL 홈(~/).
REM WSL2 localhost 포워딩으로 Windows 의 CognitiveEngine/UE5 가 127.0.0.1:8001 직통.
REM LD_LIBRARY_PATH = torch 번들 nvidia libs (cuDNN9 등) — onnxruntime-gpu CUDA provider 로드용.
REM   미설정 시 flow.decoder onnx 가 CPU 폴백 → RTF 급증(병목).
REM 기동 로직은 run_tts_wsl.sh 로 분리 — cmd.exe 가 인라인 $()·괄호를 mangle 해 SITE 가
REM 빈 값이 되던 문제(LD_LIBRARY_PATH 깨짐 → onnx CPU 폴백) 회피.
for /f "usebackq tokens=*" %%i in (`wsl wslpath "%~dp0."`) do set WSL_REPO=%%i
wsl bash "%WSL_REPO%/OmniAgent_VR_System/TTSService/run_tts_wsl.sh"
pause
