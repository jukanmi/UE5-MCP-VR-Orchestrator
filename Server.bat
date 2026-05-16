@echo off
REM LLM Cognitive Engine (FastAPI:8000) — UE5 NPC AI 오케스트레이터.
REM .venv 의 Python 으로 실행 (transformers/torch CUDA 호환 필수).
cd /d "%~dp0\OmniAgent_VR_System\CognitiveEngine"
"%~dp0\.venv\Scripts\python.exe" -m uvicorn app.main:app --port 8000
pause
