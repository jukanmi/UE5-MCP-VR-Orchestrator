@echo off
REM LLM Cognitive Engine (8000) — 음성(TTS/ASR) 서비스는 2026-09-12 폐기.

cd /d "%~dp0\OmniAgent_VR_System\CognitiveEngine"
"%~dp0\.venv\Scripts\python.exe" -m uvicorn app.main:app --port 8000
pause
