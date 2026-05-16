@echo off
REM TTS M1 스텁 서버 (FastAPI:8001) — 사인파 pcm_s16le 청크 스트리밍.
REM 루트 디렉토리에서 실행해야 OmniAgent_VR_System.TTSService.server 패키지 import 됨.
cd /d "%~dp0"
python -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001
pause
