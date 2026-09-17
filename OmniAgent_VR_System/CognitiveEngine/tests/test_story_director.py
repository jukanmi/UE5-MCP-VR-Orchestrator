"""디렉터 — side_surface 필터·LLM 폴백·dirty 게이트, 그리고 main.py 훅(Story 블록·Stage2 디렉티브)."""

import asyncio
import json
from unittest.mock import AsyncMock, patch

import pytest

from app import story as story_mod
from app.story.director import StoryDirectorResponse, run_director
from app.story.state import SIDE_ACTIVE
from story_fixtures import make_machine

AUTH = "omniagent-dev-secret-changeme-before-production"


def _resp(**kw) -> StoryDirectorResponse:
    base = dict(
        npc_goals=[{"npc_id": "elara", "goal": "각색된 목표", "hint": "은근히"}],
        quest_log="각색된 로그",
        side_surface=[],
    )
    base.update(kw)
    return StoryDirectorResponse(**base)


def test_side_surface_filtered_to_available(tmp_path):
    m = make_machine(tmp_path)
    m.state.side["s1"] = "available"
    with patch("app.story.director.ollama_structured", AsyncMock(return_value=_resp(side_surface=["s1", "s_evil"]))):
        d = asyncio.run(run_director(m, "test"))
    assert d["side_surface"] == ["s1"]
    assert d["npc_goals"]["Elara"] == {"goal": "각색된 목표", "hint": "은근히"}  # 대소문자 → 작가 철자
    assert d["npc_goals"]["Guard"]["goal"] == "무기를 권한다"  # LLM 이 빠뜨린 NPC 는 원문 보충
    asyncio.run(m.apply_direction(d))
    assert m.state.side["s1"] == SIDE_ACTIVE and m.state.director_cache["quest_log"] == "각색된 로그"


def test_llm_failure_falls_back_to_author_text(tmp_path, caplog):
    m = make_machine(tmp_path)
    with patch("app.story.director.ollama_structured", AsyncMock(side_effect=TimeoutError("cloud down"))):
        d = asyncio.run(run_director(m, "test"))
    assert d["npc_goals"] == {
        "Elara": {"goal": "용 이야기를 흘린다", "hint": ""},
        "Guard": {"goal": "무기를 권한다", "hint": ""},
    }
    assert d["quest_log"] == "q1" and d["side_surface"] == []
    assert sum("디렉터 LLM 실패" in r.message for r in caplog.records) == 1


def test_director_called_only_when_dirty(tmp_path):
    m = make_machine(tmp_path)
    llm = AsyncMock(return_value=_resp())
    with patch("app.story.director.ollama_structured", llm):
        asyncio.run(story_mod.direct_if_needed(m, "boot"))  # 캐시 없음 → 1회
        asyncio.run(story_mod.direct_if_needed(m, "replan"))  # 캐시 있음 → 0회
        asyncio.run(m.on_trigger("talked_to", {"npc_id": "Elara"}))  # 무전이
        asyncio.run(story_mod.direct_if_needed(m, "replan"))
        assert llm.await_count == 1
        asyncio.run(m.on_trigger("talked_to", {"npc_id": "Elara"}))  # 전이 → dirty
        asyncio.run(story_mod.direct_if_needed(m, "replan"))
        assert llm.await_count == 2


# ── main.py 훅 ────────────────────────────────────────────────────────


@pytest.fixture
def machine(tmp_path):
    m = make_machine(tmp_path)
    story_mod.set_story(m)
    yield m
    story_mod.set_story(None)


def _envelope(type_: str, payload: dict):
    from app.schemas.envelope import MessageEnvelope

    return MessageEnvelope(msg_id="t", type=type_, auth_token=AUTH, payload=payload)


def test_combat_victory_emits_story_block_on_transition(machine):
    from app.main import _handle_emergency_report

    machine.state.main_beat = "b3"
    machine.needs_direction = False  # 캐시 있는 상태 가정
    victory = {
        "agent_id": "Guard",
        "report_type": "combat_victory",
        "generated_at": 1.0,
        "perceptions": [{"target_id": "Bandit", "sense_type": "Sight", "distance": 1.0, "location": {}}],
    }
    with patch("app.story.director.ollama_structured", AsyncMock(return_value=_resp())):
        out = json.loads(asyncio.run(_handle_emergency_report(_envelope("emergency_report", victory))))
        assert "Story" not in out  # 다른 target → 무전이 → 블록 생략
        victory["perceptions"][0]["target_id"] = "DragonBoss"
        out = json.loads(asyncio.run(_handle_emergency_report(_envelope("emergency_report", victory))))
    assert out["Story"]["beat_id"] == "end" and out["ActionBatches"] == {}


def test_story_event_sets_flag_and_transitions(machine):
    from app.main import _handle_story_event

    machine.state.main_beat = "b2"
    machine.needs_direction = False
    env = _envelope("story_event", {"event": "flag", "name": "entered_cave"})
    with patch("app.story.director.ollama_structured", AsyncMock(return_value=_resp(quest_log="용을 잡자"))):
        out = json.loads(asyncio.run(_handle_story_event(env)))
    assert machine.state.flags["entered_cave"] is True
    assert out["Story"] == {"beat_id": "b3", "quest_log": "용을 잡자", "side": [], "events": []}


def test_replan_prompt_injects_story_directive(machine):
    """replan 턴: AgentState.story_directive 채움 → Stage2 sections 에 STORY DIRECTIVE 블록."""
    from app.agents.subgraphs.dialogue import _story_directive_block
    from app.main import _build_prompt_state

    machine.state.director_cache = {"npc_goals": {"Elara": {"goal": "G", "hint": "H"}}, "quest_log": "q"}
    machine.needs_direction = False
    prompt = {"player_id": "P", "voice_transcript": "hi", "target_npc_id": "elara", "requires_replan": True}
    with patch("app.main.spawn_background"):
        state = asyncio.run(_build_prompt_state(_envelope("prompt", prompt)))
    assert state["story_directive"] == {"elara": {"goal": "G", "hint": "H"}}
    block = _story_directive_block(state["story_directive"], {"elara": "..."})
    assert block.startswith("=== STORY DIRECTIVE ===\nelara: G — H")
    assert _story_directive_block(state["story_directive"], {"Guard": "..."}) == ""  # 대상 외 NPC 는 생략


def test_talked_to_counted_only_after_successful_turn(machine):
    from app.main import _handle_prompt
    from app.schemas.actions import ActionBatch

    prompt = {
        "player_id": "P",
        "voice_transcript": "hi",
        "target_npc_id": "Elara",
        "requires_replan": False,
        "current_plan": {"Elara": {"goal": "x"}},
    }
    ok = {"action_batches": {"Elara": ActionBatch(AgentID="Elara", Actions=[])}, "npc_plans": {}}
    bad = {"action_batches": {}, "has_error": True}
    with patch("app.main.app_graph.ainvoke", AsyncMock(return_value=bad)):
        asyncio.run(_handle_prompt(_envelope("prompt", prompt)))
    assert machine.state.talk_counts == {}
    with patch("app.main.app_graph.ainvoke", AsyncMock(return_value=ok)):
        asyncio.run(_handle_prompt(_envelope("prompt", prompt)))
    assert machine.state.talk_counts == {"Elara": 1}


# ── 라이브 ────────────────────────────────────────────────────────────


@pytest.mark.llm
def test_live_cloud_director_parses(tmp_path):
    """gemma4:cloud 실호출 — Ollama 미기동/네트워크 없음이면 skip."""
    import httpx

    from app.utils.llm_factory import OLLAMA_BASE_URL

    try:
        httpx.get(f"{OLLAMA_BASE_URL}/api/tags", timeout=2.0).raise_for_status()
    except Exception as e:
        pytest.skip(f"Ollama 없음: {e}")
    m = make_machine(tmp_path)
    # 공용 httpx 클라이언트는 첫 이벤트루프에 묶인다 — 앞선 테스트의 닫힌 루프를 물려받지 않게 초기화.
    with (
        patch("app.utils.llm_factory._ollama_client", None),
        patch("app.story.director._fallback", side_effect=AssertionError("LLM 실패 → 폴백 탔음")),
    ):
        d = asyncio.run(run_director(m, "live"))
    assert set(d["npc_goals"]) == {"Elara", "Guard"} and d["quest_log"]
