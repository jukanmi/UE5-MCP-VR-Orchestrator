@echo off
REM LLM Cognitive Engine (8000) + TTS Service (8001) + ASR Service (8002)

REM TTS 백그라운드(별도 창) — MeloTTS+OpenVoice, Windows .venv.
REM OV_USE_GPU=0: VRAM 절약 위해 CPU 합성(GPU 되돌리려면 줄 제거 또는 =1).
start "TTS Service (8001)" /D "%~dp0" cmd /c "set OV_USE_GPU=0&& "%~dp0\.venv\Scripts\python.exe" -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001"

REM ASR 백그라운드(별도 창) — faster-whisper. CPU 강제(VRAM 절약): cpu/int8/small.
REM GPU·고품질로 되돌리려면 아래 set 세 줄 제거(기본 cuda/float16/large-v3).
start "ASR Service (8002)" /D "%~dp0" cmd /c "set WHISPER_DEVICE=cpu&& set WHISPER_COMPUTE=int8&& set WHISPER_MODEL_SIZE=small&& "%~dp0\.venv\Scripts\python.exe" -m uvicorn OmniAgent_VR_System.ASRService.server:app --host 127.0.0.1 --port 8002"

REM LLM 은 현재 창에서 포그라운드 실행
cd /d "%~dp0\OmniAgent_VR_System\CognitiveEngine"
"%~dp0\.venv\Scripts\python.exe" -m uvicorn app.main:app --port 8000
pause
