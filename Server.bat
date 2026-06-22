@echo off
REM LLM Cognitive Engine (8000) + TTS Service (8001) + ASR Service (8002)

REM WSL 경로 산출 — TTS 기동에 필요
for /f "usebackq tokens=*" %%i in (`wsl wslpath "%~dp0."`) do set WSL_REPO=%%i

REM TTS 백그라운드(별도 창) — CosyVoice2 는 WSL2(Linux) 전용, Windows .venv 사용 불가.
REM 기동 로직은 run_tts_wsl.sh 로 분리 — cmd.exe 가 인라인 $()·괄호를 mangle 하는 문제 회피.
start "TTS Service (8001)" wsl bash "%WSL_REPO%/OmniAgent_VR_System/TTSService/run_tts_wsl.sh"

REM ASR 백그라운드(별도 창) — faster-whisper. CPU 강제(VRAM 절약): cpu/int8/small.
REM GPU·고품질로 되돌리려면 아래 set 세 줄 제거(기본 cuda/float16/large-v3).
start "ASR Service (8002)" /D "%~dp0" cmd /c "set WHISPER_DEVICE=cpu&& set WHISPER_COMPUTE=int8&& set WHISPER_MODEL_SIZE=small&& "%~dp0\.venv\Scripts\python.exe" -m uvicorn OmniAgent_VR_System.ASRService.server:app --host 127.0.0.1 --port 8002"

REM LLM 은 현재 창에서 포그라운드 실행
cd /d "%~dp0\OmniAgent_VR_System\CognitiveEngine"
"%~dp0\.venv\Scripts\python.exe" -m uvicorn app.main:app --port 8000
pause
