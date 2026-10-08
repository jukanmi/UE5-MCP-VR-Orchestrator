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
