"""
File: test_party_update.py
Purpose: party_update 수신 → 서버 일행 집합 반영 → 프롬프트 노출 회귀 테스트.

검증 항목:
  - join 은 집합에 추가, 같은 join 반복은 멱등(재연결 재송신 대비)
  - 멤버가 아닌 leave 는 무시, 멤버 leave 는 제거
  - 일행이 있으면 Stage1 프롬프트 끝에 "현재 일행: [...]" 한 줄, 비면 생략
"""

import asyncio
import json
import os
import sys
from unittest.mock import AsyncMock, patch

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import pytest

from app.schemas.actions import DialogueResponse
from app.schemas.envelope import MessageEnvelope
from app.server_state import STATE


@pytest.fixture(autouse=True)
def _clean_party():
    STATE.party_members.clear()
    yield
    STATE.party_members.clear()


def _send(agent_id: str, change: str) -> dict:
    from app.main import _handle_party_update

    env = MessageEnvelope(
        msg_id="test_party_001",
        type="party_update",
        auth_token="omniagent-dev-secret-changeme-before-production",
        payload={"agent_id": agent_id, "change": change},
    )
    return json.loads(_handle_party_update(env))


def test_join_is_idempotent_and_leave_ignores_non_member():
    _send("Elara", "join")
    _send("Elara", "join")
    assert STATE.party_members == {"Elara"}

    _send("James", "leave")  # 멤버 아님 — 무시
    assert STATE.party_members == {"Elara"}

    result = _send("Elara", "leave")
    assert STATE.party_members == set()
    assert not result.get("ActionBatches")  # 통보 전용 — 행동 없음


def _stage1_system_prompt() -> str:
    from app.agents.subgraphs.dialogue import _run_stage1_llm, _Stage1Context

    fmt = dict(
        name="Elara", role="Healer", traits="kind", speech_style="", memory="None", sentiment="Neutral",
        story_goal="None", rag_context="None", chat_history="None", inventory="None", valid_targets="Player",
    )
    ctx = _Stage1Context(fmt, None, "hi", "Player said: hi")
    resp = DialogueResponse(mode="Common", facial="Neutral", speech="안녕", actions=[])
    with patch("app.agents.subgraphs.dialogue.ollama_structured", new_callable=AsyncMock, return_value=resp) as llm:
        asyncio.run(_run_stage1_llm(ctx, "Elara"))
    return llm.await_args.args[0]


def test_party_line_in_prompt_only_when_non_empty():
    assert "현재 일행" not in _stage1_system_prompt()

    _send("James", "join")
    _send("Elara", "join")
    assert _stage1_system_prompt().rstrip().endswith("현재 일행: [Elara, James]")


def test_new_connection_clears_stale_members():
    """WS 만 끊긴 동안의 leave 는 C++ 가 버린다 — 재연결 시 서버 집합을 비워 유령 멤버를 남기지 않는다."""
    from fastapi import WebSocketDisconnect

    from app.main import websocket_llm_endpoint

    class _FakeWS:
        async def accept(self):
            pass

        async def receive_text(self):
            raise WebSocketDisconnect()

        async def send_text(self, text):
            pass

    _send("Ghost", "join")
    asyncio.run(websocket_llm_endpoint(_FakeWS()))
    assert STATE.party_members == set()


# ── M3: 호감도 게이트 · 피격 무시 ─────────────────────────────────────────────


def _resp(*action_types: str, speech: str = "좋아요, 같이 가요!") -> DialogueResponse:
    from app.schemas.actions import DialogueActionItem

    return DialogueResponse(
        mode="Common", facial="Neutral", speech=speech, actions=[DialogueActionItem(type=t) for t in action_types]
    )


def _gate(resp: DialogueResponse, npc: str, score: int) -> DialogueResponse:
    from app.agents.subgraphs.dialogue import _gate_party_actions
    rel = type("Rel", (), {"affinity_score": score})()
    with patch("app.agents.subgraphs.dialogue.db_manager.get_affinity", new_callable=AsyncMock, return_value=rel):
        asyncio.run(_gate_party_actions(resp, npc, "Player"))
    return resp


def test_join_below_threshold_is_refused_with_fixed_line():
    from app.agents.subgraphs.dialogue import _PARTY_REFUSE_LOW_AFFINITY, PARTY_JOIN_AFFINITY
    from app.schemas.actions import DialogueActionItem

    resp = _resp("Follow", "JoinParty", "Scan")
    resp.actions[0].target = "Player"
    resp.facial, resp.tone = "Happy", "cheerfully"
    resp.actions.append(DialogueActionItem(type="Follow", target="Goblin"))
    r = _gate(resp, "Elara", PARTY_JOIN_AFFINITY - 1)
    # 플레이어 대상 Follow·JoinParty 는 제거, 무관 액션(Scan, 다른 대상 Follow)은 유지
    assert [(a.type, a.target) for a in r.actions] == [("Scan", ""), ("Follow", "Goblin")]
    assert r.speech in _PARTY_REFUSE_LOW_AFFINITY
    assert r.facial == "Neutral" and r.tone == ""


def test_affinity_lookup_failure_refuses():
    from app.agents.subgraphs.dialogue import _gate_party_actions

    r = _resp("JoinParty")
    with patch(
        "app.agents.subgraphs.dialogue.db_manager.get_affinity", new_callable=AsyncMock, side_effect=RuntimeError("db")
    ):
        asyncio.run(_gate_party_actions(r, "Elara", "Player"))
    assert r.actions == [] and r.speech != "좋아요, 같이 가요!"


def test_join_at_threshold_passes_untouched():
    from app.agents.subgraphs.dialogue import PARTY_JOIN_AFFINITY

    r = _gate(_resp("JoinParty"), "Elara", PARTY_JOIN_AFFINITY)
    assert [a.type for a in r.actions] == ["JoinParty"]
    assert r.speech == "좋아요, 같이 가요!"


def test_join_when_party_full_is_refused():
    from app.agents.subgraphs.dialogue import _PARTY_REFUSE_FULL, PARTY_MAX_SIZE

    STATE.party_members.update(f"M{i}" for i in range(PARTY_MAX_SIZE))
    r = _gate(_resp("JoinParty"), "Elara", 100)
    assert r.actions == []
    assert r.speech in _PARTY_REFUSE_FULL


def test_member_join_dropped_and_non_member_leave_dropped_speech_kept():
    STATE.party_members.add("Elara")
    r = _gate(_resp("JoinParty"), "Elara", 0)
    assert r.actions == [] and r.speech == "좋아요, 같이 가요!"

    r = _gate(_resp("LeaveParty"), "James", 100)  # 비멤버
    assert r.actions == []

    r = _gate(_resp("LeaveParty"), "Elara", 0)  # 멤버는 통과
    assert [a.type for a in r.actions] == ["LeaveParty"]


def test_party_vocab_always_in_prompt_and_stop_semantics():
    prompt = _stage1_system_prompt()
    assert "JoinParty" in prompt and "LeaveParty" in prompt and "Stop ONLY" in prompt


def _hostile_calls(agent: str, target: str) -> list:
    from app.main import _apply_hostile_affinity

    p = type("P", (), {"danger_score": 0.8, "target_id": target, "sense_type": "Sight"})()
    with patch("app.main.db_manager.get_affinity", new_callable=AsyncMock), patch(
        "app.main.db_manager.update_affinity_sync"
    ) as upd:
        asyncio.run(_apply_hostile_affinity(agent, [p]))
    return upd.call_args_list


def test_hostile_affinity_skipped_only_between_party_members():
    STATE.party_members.update({"Elara", "James"})
    assert _hostile_calls("Elara", "James") == []  # 파티원끼리 — 감점 없음
    assert _hostile_calls("James", "Elara") == []  # 반대 방향도 동일
    assert len(_hostile_calls("Elara", "Player")) == 1  # 플레이어는 집합 밖 — 정상 감점
    assert len(_hostile_calls("Elara", "Goblin")) == 1
