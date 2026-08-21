"""
File: test_emergency_notify_only.py
Purpose: emergency_report 통보 전용화 회귀 테스트 (SPEC_reflex_table §3.5).

배경: 반사 판단은 C++ 척수반사 테이블이 0ms 로 끝낸다. 파이썬은 인지·호감도만 갱신하고
행동은 만들지 않는다. 여기서 행동이 실린 배치가 나가면 UE5 쪽에서 두 가지가 깨진다.
  1) 반사가 이미 실행한 액션과 중복 지시가 큐에 쌓인다.
  2) 배치에 실린 Mode 가 반사가 방금 올린 Combat 을 되돌린다.

검증 항목:
  - danger 게이트 통과(고위험) → 행동 없는 빈 배치
  - danger 게이트 미달(저위험) → 행동 없는 빈 배치
  - reflex_action 이 실려오면 메모리에 Event 로 기록
  - reflex_action 미포함(구 C++ 클라이언트) → 기록 없이 정상 처리(하위호환)
"""

import sys
import os
import json
import asyncio
from unittest.mock import patch, AsyncMock

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from app.schemas.envelope import MessageEnvelope, EmergencyReportPayload, PerceptionData


def _envelope(danger: float, reflex_action: str = "") -> MessageEnvelope:
    """emergency_report 엔벨로프 1건 조립."""
    payload = {
        "agent_id": "Moca",
        "perceptions": [
            {
                "target_id": "Bandit",
                "sense_type": "Sight",
                "distance": 300.0,
                "danger_score": danger,
                "location": {"x": 0.0, "y": 0.0, "z": 0.0},
            }
        ],
        "generated_at": 1234.0,
    }
    if reflex_action:
        payload["reflex_action"] = reflex_action

    return MessageEnvelope(
        msg_id="test_reflex_001",
        type="emergency_report",
        auth_token="omniagent-dev-secret-changeme-before-production",
        payload=payload,
    )


def _run(envelope: MessageEnvelope) -> dict:
    from app.main import _handle_emergency_report

    return json.loads(asyncio.run(_handle_emergency_report(envelope)))


def test_reflex_action_field_is_optional():
    """구 C++ 클라이언트는 필드를 생략한다 — 기본값 빈 문자열로 파싱돼야 한다."""
    payload = EmergencyReportPayload(
        agent_id="Moca",
        perceptions=[],
        generated_at=1234.0,
    )
    assert payload.reflex_action == ""
    assert payload.report_type == "perception"


def test_high_danger_returns_no_actions():
    """게이트를 통과해도 행동을 만들지 않는다 — 반사는 C++ 담당."""
    # 호감도 감점은 DB 쓰기라 여기선 관심 밖 — 호출됐는지만 보고 실제 I/O 는 막는다.
    with patch("app.main._apply_hostile_affinity", new_callable=AsyncMock) as affinity:
        result = _run(_envelope(danger=0.9))

    affinity.assert_awaited_once()
    batches = result.get("ActionBatches", {})
    assert all(not b.get("Actions") for b in batches.values()), f"행동이 실려 나감: {result}"


def test_low_danger_returns_no_actions():
    """저위험도 통보만 — 게이트 아래에서는 호감도 감점 대상도 아니다."""
    result = _run(_envelope(danger=0.1))

    batches = result.get("ActionBatches", {})
    assert all(not b.get("Actions") for b in batches.values()), f"행동이 실려 나감: {result}"


def test_reflex_action_recorded_to_memory():
    """반사 이력이 오면 메모리에 남겨야 다음 replan 이 중복 지시를 안 한다."""
    with patch("app.main._record_reflex_memory") as recorder:
        _run(_envelope(danger=0.1, reflex_action="Attack"))

    recorder.assert_called_once_with("Moca", "Attack")


def test_no_reflex_action_no_memory_write():
    """반사 미발동이면 기록도 없어야 한다(빈 문자열로 오탐 기록 금지)."""
    with patch("app.main._record_reflex_memory") as recorder:
        _run(_envelope(danger=0.1))

    recorder.assert_not_called()
