"""
SPEC_llm_perf 검증 — 웜업 스로틀·실패 삼킴·모델 ID·디바운스.
"""

import asyncio
import threading
import time
import types
from unittest.mock import AsyncMock, MagicMock, patch

import pytest


# ──────────────────────────────────────────────────────────────────────────────
# _prewarm_core_llm
# ──────────────────────────────────────────────────────────────────────────────

def _make_main_module():
    """main 모듈의 핵심 심볼만 임포트해 테스트 격리."""
    import importlib, sys
    if "app.main" in sys.modules:
        return sys.modules["app.main"]
    return importlib.import_module("app.main")


def _reset_prewarm(mod):
    mod._last_core_prewarm = 0.0


def test_prewarm_calls_http_once():
    """30s 안 2회 호출 시 HTTP 1회만."""
    mod = _make_main_module()
    _reset_prewarm(mod)

    post_mock = AsyncMock(return_value=MagicMock(status_code=200))
    client_mock = MagicMock()
    client_mock.post = post_mock

    async def run():
        with patch.object(mod.llm_factory, "get_ollama_client", return_value=client_mock):
            await mod._prewarm_core_llm()
            await mod._prewarm_core_llm()  # 스로틀 — 무시

    asyncio.run(run())
    assert post_mock.call_count == 1


def test_prewarm_swallows_exception():
    """HTTP 예외 발생해도 전파 없음."""
    mod = _make_main_module()
    _reset_prewarm(mod)

    client_mock = MagicMock()
    client_mock.post = AsyncMock(side_effect=Exception("connection refused"))

    async def run():
        with patch.object(mod.llm_factory, "get_ollama_client", return_value=client_mock):
            await mod._prewarm_core_llm()  # should not raise

    asyncio.run(run())  # 예외 전파 없으면 통과


def test_prewarm_uses_stage2_model_not_default():
    """요청 바디가 MODELS[STAGE2_MODEL] — DEFAULT_MODEL 회귀 방지."""
    mod = _make_main_module()
    _reset_prewarm(mod)

    captured_bodies = []

    async def fake_post(url, json=None, **kwargs):
        captured_bodies.append(json)
        return MagicMock(status_code=200)

    client_mock = MagicMock()
    client_mock.post = fake_post

    async def run():
        with patch.object(mod.llm_factory, "get_ollama_client", return_value=client_mock):
            await mod._prewarm_core_llm()

    asyncio.run(run())

    assert captured_bodies, "POST not called"
    from app.utils import llm_factory
    expected_model = llm_factory.MODELS[llm_factory.STAGE2_MODEL]
    assert captured_bodies[0]["model"] == expected_model


def test_prewarm_body_has_no_prompt_key():
    """요청 바디에 prompt 키 없음 — 로드콜 형태 회귀 방지."""
    mod = _make_main_module()
    _reset_prewarm(mod)

    captured_bodies = []

    async def fake_post(url, json=None, **kwargs):
        captured_bodies.append(json)
        return MagicMock(status_code=200)

    client_mock = MagicMock()
    client_mock.post = fake_post

    async def run():
        with patch.object(mod.llm_factory, "get_ollama_client", return_value=client_mock):
            await mod._prewarm_core_llm()

    asyncio.run(run())
    assert "prompt" not in captured_bodies[0]


# ──────────────────────────────────────────────────────────────────────────────
# memory_manager 디바운스
# ──────────────────────────────────────────────────────────────────────────────

from app.utils.memory_manager import ConversationMemory, SUMMARIZE_DEBOUNCE_S


def _make_memory(tmp_path, agent_id="test_npc") -> ConversationMemory:
    import app.utils.memory_manager as mm
    mm.MEMORY_BASE_PATH = str(tmp_path)
    m = ConversationMemory(agent_id)
    return m


def _force_budget_exceeded(mem: ConversationMemory):
    """임계치를 이미 초과한 상태를 직접 주입."""
    from app.utils.memory_manager import MemoryEntry, MAX_TOKENS_PER_NPC, SUMMARIZE_THRESHOLD
    import math
    target = math.ceil(MAX_TOKENS_PER_NPC * SUMMARIZE_THRESHOLD) + 100
    # 한글 문자 — 0.6 tok/char
    chars_needed = math.ceil(target / 0.6) + 100
    with mem.lock:
        mem.entries.append(MemoryEntry(
            timestamp="2026-01-01T00:00:00",
            speaker="Player",
            content="가" * chars_needed,
            is_summary=False,
        ))


def test_debounce_timer_set_when_budget_exceeded(tmp_path):
    """예산 초과 시 타이머가 예약된다."""
    mem = _make_memory(tmp_path)
    _force_budget_exceeded(mem)

    with patch.object(mem, "_schedule_summarize_locked") as sched:
        with patch.object(mem, "_save_to_file"):
            mem.add_entry("Player", "안녕")
        assert sched.called


def test_no_timer_when_budget_ok(tmp_path):
    """예산 미달이면 타이머 미예약."""
    mem = _make_memory(tmp_path)

    with patch.object(mem, "_schedule_summarize_locked") as sched:
        with patch.object(mem, "_save_to_file"):
            mem.add_entry("Player", "hi")
        assert not sched.called


def test_debounce_fires_once_after_last_entry(tmp_path):
    """연속 add_entry 시 요약 1회만, 마지막 호출 후 실행."""
    FAST = 0.05  # 테스트용 디바운스 0.05s
    import app.utils.memory_manager as mm
    original = mm.SUMMARIZE_DEBOUNCE_S
    mm.SUMMARIZE_DEBOUNCE_S = FAST

    summarize_calls = []

    mem = _make_memory(tmp_path)
    _force_budget_exceeded(mem)

    original_check = mem._check_and_summarize

    def counting_check():
        summarize_calls.append(1)

    mem._check_and_summarize = counting_check

    try:
        with patch.object(mem, "_save_to_file"):
            for _ in range(3):
                mem.add_entry("Player", "가" * 10)
                time.sleep(0.01)  # 타이머 만료 전에 다음 호출로 리셋

        # 마지막 add_entry 이후 타이머 만료 대기
        time.sleep(FAST * 3)
    finally:
        mm.SUMMARIZE_DEBOUNCE_S = original
        mem._check_and_summarize = original_check

    assert len(summarize_calls) == 1, f"요약 {len(summarize_calls)}회 (기대: 1)"
