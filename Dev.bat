@echo off
REM LLM Cognitive Engine 개발 모드 — 코드 수정 시 자동 재기동 (uvicorn --reload).
REM Python 수정 후 "서버 재시작해야 반영" 수동 반복을 없애는 용도.
REM PYTHONIOENCODING: stdout 이 파이프/리다이렉트일 때 cp949 인코딩 크래시 방지.

cd /d "%~dp0\OmniAgent_VR_System\CognitiveEngine"
set PYTHONIOENCODING=utf-8
"%~dp0\.venv\Scripts\python.exe" -m uvicorn app.main:app --port 8000 --reload --reload-dir app
pause
