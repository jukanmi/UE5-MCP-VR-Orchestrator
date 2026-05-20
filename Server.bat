@echo off
REM LLM Cognitive Engine (8000) + TTS Service (8001) + ASR Service (8002)

REM TTS 백그라운드(별도 창)
start "TTS Service (8001)" /D "%~dp0" "%~dp0\.venv\Scripts\python.exe" -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001

REM ASR 백그라운드(별도 창) — 현재 스텁(echo) 단계
start "ASR Service (8002)" /D "%~dp0" "%~dp0\.venv\Scripts\python.exe" -m uvicorn OmniAgent_VR_System.ASRService.server:app --host 127.0.0.1 --port 8002

REM LLM 은 현재 창에서 포그라운드 실행
cd /d "%~dp0\OmniAgent_VR_System\CognitiveEngine"
"%~dp0\.venv\Scripts\python.exe" -m uvicorn app.main:app --port 8000
pause
