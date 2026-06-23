@echo off
REM TTS M2 Server (FastAPI 8001) - VibeVoice-Realtime-0.5B
cd /d "%~dp0"
".venv\Scripts\python.exe" -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001
pause
