@echo off
REM LLM Cognitive Engine (FastAPI 8000)
cd /d "%~dp0\OmniAgent_VR_System\CognitiveEngine"
"%~dp0\.venv\Scripts\python.exe" -m uvicorn app.main:app --port 8000
pause
