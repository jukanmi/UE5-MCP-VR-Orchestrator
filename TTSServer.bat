@echo off
REM TTS M2 서버 (FastAPI:8001) — microsoft/VibeVoice-Realtime-0.5B.
REM 루트 디렉토리에서 실행해야 OmniAgent_VR_System.TTSService.server 패키지 import.
REM .venv 의 torch (cu128) 가 sm_120 (RTX 5070 Ti Blackwell) 호환 필수.
cd /d "%~dp0"
".venv\Scripts\python.exe" -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001
pause
