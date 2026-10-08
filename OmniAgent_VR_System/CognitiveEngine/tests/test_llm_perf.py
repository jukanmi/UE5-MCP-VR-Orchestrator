"""
SPEC_llm_perf 검증 — 웜업 스로틀·실패 삼킴·모델 ID·디바운스.
"""

import asyncio
import time
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from app.utils.memory_manager import ConversationMemory


# ──────────────────────────────────────────────────────────────────────────────
# _prewarm_core_llm
# ──────────────────────────────────────────────────────────────────────────────


def _make_main_module():
    """main 모듈의 핵심 심볼만 임포트해 테스트 격리."""
    import importlib
    import sys

    if "app.main" in sys.modules:
        return sys.modules["app.main"]
    return importlib.import_module("app.main")


def _reset_prewarm(mod):
    mod.STATE.last_core_prewarm = 0.0


@pytest.fixture(autouse=True)
def _local_stage2(monkeypatch):
    """prewarm 테스트는 로컬 Stage2 모드 기준 — 클라우드 모드에선 prewarm 이 생략된다(별도 테스트)."""
    from app.utils import llm_factory

    monkeypatch.setattr(llm_factory, "STAGE2_CLOUD_ENABLED", False)


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


def test_prewarm_skipped_in_cloud_mode(monkeypatch):
    """Stage2 클라우드 모드에선 로컬 폴백 모델을 올리지 않는다(HTTP 0회)."""
    from app.utils import llm_factory

    monkeypatch.setattr(llm_factory, "STAGE2_CLOUD_ENABLED", True)
    mod = _make_main_module()
    _reset_prewarm(mod)
    post_mock = AsyncMock()
    client_mock = MagicMock(post=post_mock)

    async def run():
        with patch.object(mod.llm_factory, "get_ollama_client", return_value=client_mock):
            await mod._prewarm_core_llm()

    asyncio.run(run())
    assert post_mock.call_count == 0


# ──────────────────────────────────────────────────────────────────────────────
# SLM keep_alive·핑
# ──────────────────────────────────────────────────────────────────────────────


def test_keep_alive_policy():
    from app.utils import llm_factory as lf

    assert lf._keep_alive_for(lf.STAGE1_MODEL) == "30m"
    assert lf._keep_alive_for(lf.STAGE2_MODEL) == "30s"
    assert lf._keep_alive_for("gemma4") == "5m"


def test_ping_slm_is_load_call_on_slm():
    """핑 = e4b 빈 프롬프트 로드콜, keep_alive 는 SLM_KEEP_ALIVE."""
    mod = _make_main_module()
    bodies = []

    async def fake_post(url, json=None, **kwargs):
        bodies.append(json)
        return MagicMock(status_code=200)

    async def run():
        with patch.object(mod.llm_factory, "get_ollama_client", return_value=MagicMock(post=fake_post)):
            await mod._ping_slm()

    asyncio.run(run())
    assert bodies[0] == {"model": mod.llm_factory.MODELS["gemma4_slm"], "keep_alive": mod.llm_factory.SLM_KEEP_ALIVE}


def test_keepalive_loop_pings_periodically_and_stops_on_cancel(monkeypatch):
    mod = _make_main_module()
    monkeypatch.setattr(mod.llm_factory, "SLM_PING_INTERVAL_S", 0.01)
    ping = AsyncMock()

    async def run():
        with patch.object(mod, "_ping_slm", ping):
            task = asyncio.create_task(mod._slm_keepalive_loop())
            await asyncio.sleep(0.08)
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            n = ping.call_count
            await asyncio.sleep(0.05)
            return n, task.cancelled()

    n, cancelled = asyncio.run(run())
    assert n >= 2 and cancelled and ping.call_count == n


# ──────────────────────────────────────────────────────────────────────────────
# Stage2 클라우드 우선 + 로컬 폴백
# ──────────────────────────────────────────────────────────────────────────────


@pytest.fixture(autouse=True)
def _reset_stage2_cooldown(monkeypatch):
    """Stage2 연속 실패 카운터·쿨다운은 모듈 전역 — 테스트 간 누수 차단."""
    from app.utils import llm_factory as lf

    monkeypatch.setattr(lf, "_stage2_cloud_fails", 0)
    monkeypatch.setattr(lf, "_stage2_cloud_skip_until", 0.0)


def _stage2_calls(monkeypatch, enabled, side_effect, **kw):
    """stage2_structured 실행 후 (결과, ollama_structured 호출 kwargs 목록) 반환."""
    from app.utils import llm_factory as lf

    monkeypatch.setattr(lf, "STAGE2_CLOUD_ENABLED", enabled)
    mock = AsyncMock(side_effect=side_effect)
    with patch.object(lf, "ollama_structured", mock):
        result = asyncio.run(lf.stage2_structured("s", "u", dict, temperature=0.3, **kw))
    return result, [c.kwargs for c in mock.call_args_list]


def test_stage2_cloud_success_no_fallback(monkeypatch):
    result, calls = _stage2_calls(monkeypatch, True, ["ok"])
    assert result == "ok" and [c["model_name"] for c in calls] == ["cloud_gemma4_31b"]
    assert calls[0]["timeout"] == 4.0


def test_stage2_cloud_model_separate_from_story():
    from app.utils import llm_factory as lf

    assert lf.MODELS[lf.STAGE2_CLOUD_MODEL] == "gemma4:31b-cloud"
    assert lf.MODELS[lf.STORY_MODEL] == "gemma4:cloud"


@pytest.mark.parametrize("err", [TimeoutError("t"), ValueError("bad json"), RuntimeError("down")])
def test_stage2_falls_back_once_to_local(monkeypatch, caplog, err):
    result, calls = _stage2_calls(monkeypatch, True, [err, "local"], log_extra={"stage": "stage2"})
    assert result == "local" and [c["model_name"] for c in calls] == ["cloud_gemma4_31b", "mid"]
    assert "timeout" not in calls[1]  # 클라우드 timeout 이 폴백 호출로 새지 않는다
    assert calls[1]["log_extra"] == {"stage": "stage2", "fallback_from_cloud": True}
    assert "fallback_from_cloud" not in calls[0]["log_extra"]
    assert "[Stage2] 클라우드" in caplog.text and "폴백" in caplog.text


def test_stage2_cloud_disabled_goes_local_only(monkeypatch):
    result, calls = _stage2_calls(monkeypatch, False, ["local"], log_extra={"stage": "stage2"})
    assert result == "local" and [c["model_name"] for c in calls] == ["mid"]
    assert "fallback_from_cloud" not in calls[0]["log_extra"]


def test_stage2_cooldown_after_two_failures(monkeypatch):
    """연속 2회 실패 → 쿨다운 동안 클라우드 미호출, 시간 경과 후 재시도, 성공하면 카운터 리셋."""
    from app.utils import llm_factory as lf

    monkeypatch.setattr(lf, "STAGE2_CLOUD_ENABLED", True)
    clock = [1000.0]
    monkeypatch.setattr(lf.time, "monotonic", lambda: clock[0])
    models: list[str] = []

    async def fake(system, user, schema, **kw):
        models.append(kw["model_name"])
        if kw["model_name"] == "cloud_gemma4_31b" and len(models) <= 3:
            raise RuntimeError("down")
        return kw["model_name"]

    async def run():
        out = [await lf.stage2_structured("s", "u", dict) for _ in range(2)]  # 클라우드 실패 2회 → 쿨다운 진입
        out.append(await lf.stage2_structured("s", "u", dict))  # 쿨다운 중 — 클라우드 호출 없음
        clock[0] += lf.STAGE2_CLOUD_COOLDOWN_S + 1
        out.append(await lf.stage2_structured("s", "u", dict))  # 해제 후 재시도 → 클라우드 성공
        return out

    with patch.object(lf, "ollama_structured", fake):
        out = asyncio.run(run())
    assert out == ["mid", "mid", "mid", "cloud_gemma4_31b"]
    assert models == ["cloud_gemma4_31b", "mid", "cloud_gemma4_31b", "mid", "mid", "cloud_gemma4_31b"]
    assert lf._stage2_cloud_fails == 0 and lf._stage2_cloud_skip_until == 0.0


def test_stage2_success_resets_fail_counter(monkeypatch):
    """실패 1회 뒤 성공하면 카운터 리셋 — 이후 실패 1회로는 쿨다운에 안 들어간다."""
    from app.utils import llm_factory as lf

    seq = iter([RuntimeError("x"), "local", "ok", RuntimeError("y"), "local"])
    monkeypatch.setattr(lf, "STAGE2_CLOUD_ENABLED", True)

    async def fake(system, user, schema, **kw):
        v = next(seq)
        if isinstance(v, Exception):
            raise v
        return v

    async def run():
        for _ in range(3):
            await lf.stage2_structured("s", "u", dict)

    with patch.object(lf, "ollama_structured", fake):
        asyncio.run(run())
    assert lf._stage2_cloud_fails == 1 and lf._stage2_cloud_skip_until == 0.0


def test_stage2_cloud_timeout_falls_back(monkeypatch):
    """응답이 안 오면 STAGE2_CLOUD_TIMEOUT_S 뒤 로컬로 넘어간다."""
    from app.utils import llm_factory as lf

    monkeypatch.setattr(lf, "STAGE2_CLOUD_ENABLED", True)
    monkeypatch.setattr(lf, "STAGE2_CLOUD_TIMEOUT_S", 0.05)

    async def fake(system, user, schema, **kw):
        if kw["model_name"] == "cloud_gemma4_31b":
            await asyncio.sleep(5)
        return kw["model_name"]

    with patch.object(lf, "ollama_structured", fake):
        assert asyncio.run(lf.stage2_structured("s", "u", dict)) == "mid"


# ──────────────────────────────────────────────────────────────────────────────
# memory_manager 디바운스
# ──────────────────────────────────────────────────────────────────────────────



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
        mem.entries.append(
            MemoryEntry(
                timestamp="2026-01-01T00:00:00",
                speaker="Player",
                content="가" * chars_needed,
                is_summary=False,
            )
        )


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
