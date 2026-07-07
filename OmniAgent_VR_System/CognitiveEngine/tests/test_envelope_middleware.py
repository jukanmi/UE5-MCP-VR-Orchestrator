"""
File: test_envelope_middleware.py
Purpose: Envelope 스키마 & 미들웨어 유닛 테스트

WHY: main.py 리팩토링으로 미들웨어가 분리되었으므로,
     WebSocket 없이 순수 함수 단위로 각 케이스를 검증한다.

테스트 케이스 목록:
  [1] 정상 auth_token → 인증 통과
  [2] 잘못된 auth_token → 인증 거부
  [3] 2.5초 오래된 timestamp → Stale 패킷 감지
  [4] 현재 timestamp → 유효 패킷 통과
  [5] action_failed → failed_action_history 항목 생성 확인
  [6] state_update Envelope 파싱 확인
  [7] prompt Envelope 파싱 확인
  [8] _process_message state_update → "cached" 응답 확인 (통합)
"""
import sys
import os
import time
import json
import asyncio

# 모듈 경로 추가
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

# 테스트용 WS_AUTH_TOKEN 환경변수 설정 (import 전에 반드시 먼저 설정)
os.environ["WS_AUTH_TOKEN"] = "test-token-for-unit-test"

from app.schemas.envelope import (
    MessageEnvelope, EEnvelopeType,
    StateUpdatePayload, PromptPayload, ActionFailedPayload,
)
from app.middleware import validate_auth_token, is_stale_packet, build_failed_event


# ─────────────────────────────────────────────────────────────────────────────
# 테스트용 Envelope 팩토리 헬퍼
# WHY: 각 테스트에서 중복되는 Envelope 생성 코드를 함수로 추출.
# ─────────────────────────────────────────────────────────────────────────────

def make_prompt_envelope(token: str = "test-token-for-unit-test",
                          timestamp_offset: float = 0.0) -> MessageEnvelope:
    """prompt 타입 테스트용 Envelope 생성."""
    return MessageEnvelope(
        msg_id="test-msg-001",
        auth_token=token,
        timestamp=time.time() - timestamp_offset,
        type=EEnvelopeType.PROMPT,
        payload={
            "player_id": "Player_01",
            "voice_transcript": "이리와!",
        }
    )


def make_state_update_envelope(token: str = "test-token-for-unit-test",
                                timestamp_offset: float = 0.0) -> MessageEnvelope:
    """state_update 타입 테스트용 Envelope 생성."""
    return MessageEnvelope(
        msg_id="test-msg-002",
        auth_token=token,
        timestamp=time.time() - timestamp_offset,
        type=EEnvelopeType.STATE_UPDATE,
        payload={
            "owner_agent_id": "Elara",
            "owner_location": {"x": 100.0, "y": 200.0, "z": 0.0},
            "threat_level": "High",
            "in_cover": True,
            "line_of_sight": False,
            "perceived_targets": [
                {
                    "target_id": "Enemy_01",
                    "sense_type": "Sight",
                    "distance": 500.0,
                    "in_line_of_sight": True,
                    "location": {"x": 100.0, "y": 200.0, "z": 0.0}
                }
            ],
        }
    )


def make_action_failed_envelope(ref_msg_id: str = "req-1233") -> MessageEnvelope:
    """action_failed 타입 테스트용 Envelope 생성."""
    return MessageEnvelope(
        msg_id="test-msg-003",
        ref_msg_id=ref_msg_id,
        auth_token="test-token-for-unit-test",
        timestamp=time.time(),
        type=EEnvelopeType.ACTION_FAILED,
        payload={
            "failed_action_type": "Move",
            "reason": "PathNotFound",
            "executor_npc_id": "Elara",
        }
    )


# ═════════════════════════════════════════════════════════════════════════════
# [1] 인증 토큰 검증 테스트
# ═════════════════════════════════════════════════════════════════════════════

def test_valid_auth_token_passes():
    """올바른 토큰은 인증을 통과해야 한다."""
    assert validate_auth_token("test-token-for-unit-test") is True
    print("[PASS] test_valid_auth_token_passes")


def test_invalid_auth_token_rejected():
    """잘못된 토큰은 즉시 거부되어야 한다."""
    assert validate_auth_token("wrong-token") is False
    assert validate_auth_token("") is False
    print("[PASS] test_invalid_auth_token_rejected")


# ═════════════════════════════════════════════════════════════════════════════
# [2] Stale 패킷 감지 테스트
# ═════════════════════════════════════════════════════════════════════════════

def test_stale_packet_detected():
    """2.5초 전 timestamp는 Stale 패킷으로 감지되어야 한다."""
    stale_timestamp = time.time() - 2.5
    assert is_stale_packet(stale_timestamp, threshold_seconds=2.0) is True
    print("[PASS] test_stale_packet_detected")


def test_fresh_packet_passes():
    """현재 시간의 패킷은 유효해야 한다."""
    fresh_timestamp = time.time() - 0.5
    assert is_stale_packet(fresh_timestamp, threshold_seconds=2.0) is False
    print("[PASS] test_fresh_packet_passes")


# ═════════════════════════════════════════════════════════════════════════════
# [3] action_failed 이력 변환 테스트
# ═════════════════════════════════════════════════════════════════════════════

def test_build_failed_event_structure():
    """
    action_failed Envelope가 올바른 이력 딕셔너리로 변환되어야 한다.
    WHY: 이 딕셔너리가 AgentState.failed_action_history에 저장되므로
         모든 키가 올바르게 존재해야 한다.
    """
    envelope = make_action_failed_envelope(ref_msg_id="req-1233")
    event = build_failed_event(envelope)

    assert event["ref_msg_id"] == "req-1233"
    assert event["failed_action_type"] == "Move"
    assert event["reason"] == "PathNotFound"
    assert event["executor_npc_id"] == "Elara"
    assert "recorded_at" in event
    # recorded_at 은 UTC ISO8601 문자열 (datetime.now(timezone.utc).isoformat()).
    assert isinstance(event["recorded_at"], str)
    from datetime import datetime

    datetime.fromisoformat(event["recorded_at"])  # 파싱 가능해야 함
    print("[PASS] test_build_failed_event_structure")


# ═════════════════════════════════════════════════════════════════════════════
# [4] Envelope 파싱 테스트
# ═════════════════════════════════════════════════════════════════════════════

def test_state_update_payload_parsing():
    """state_update payload가 StateUpdatePayload 모델로 올바르게 파싱되어야 한다."""
    envelope = make_state_update_envelope()
    payload = envelope.parse_state_update_payload()

    assert payload.threat_level == "High"
    assert payload.in_cover is True
    assert len(payload.perceived_targets) == 1
    assert payload.perceived_targets[0].target_id == "Enemy_01"
    assert payload.perceived_targets[0].sense_type == "Sight"
    assert payload.owner_agent_id == "Elara"
    # 좌표 소수점 반올림 검증
    assert payload.owner_location["x"] == 100.0
    print("[PASS] test_state_update_payload_parsing")


def test_prompt_payload_parsing():
    """prompt payload가 PromptPayload 모델로 파싱되어야 한다."""
    envelope = make_prompt_envelope()
    payload = envelope.parse_prompt_payload()

    assert payload.player_id == "Player_01"
    assert payload.voice_transcript == "이리와!"
    print("[PASS] test_prompt_payload_parsing")


# ═════════════════════════════════════════════════════════════════════════════
# [5] _process_message 통합 테스트 (state_update 캐싱)
# ═════════════════════════════════════════════════════════════════════════════

def test_process_state_update_returns_cached():
    """
    state_update 메시지를 _process_llm_message로 처리하면
    LLM을 호출하지 않고 {"status": "cached"} 응답이 와야 한다.
    """
    from app.main import _process_llm_message

    envelope = make_state_update_envelope()
    raw_json = json.dumps({
        "msg_id": envelope.msg_id,
        "auth_token": envelope.auth_token,
        "timestamp": envelope.timestamp,
        "type": envelope.type.value,
        "payload": {
            "owner_agent_id": "Elara",
            "owner_location": {"x": 100.0, "y": 200.0, "z": 0.0},
            "threat_level": "High",
            "in_cover": True,
            "line_of_sight": False,
            "perceived_targets": []
        }
    })

    import traceback
    try:
        response_str = asyncio.run(_process_llm_message(raw_json))
        response = json.loads(response_str)

        assert response.get("status") == "cached", f"예상: 'cached', 실제: {response}"
        print("[PASS] test_process_state_update_returns_cached")
    except Exception as e:
        print("Exception in test_process_state_update_returns_cached:")
        traceback.print_exc()
        raise


def test_process_stale_packet_dropped():
    """Stale 패킷은 _process_llm_message에서 드랍 응답이 와야 한다."""
    from app.main import _process_llm_message

    raw_json = json.dumps({
        "msg_id": "stale-msg",
        "auth_token": "test-token-for-unit-test",
        "type": "state_update",
        "timestamp": time.time() - 15.0,   # 15초 전 → Stale (임계 10초)
        "payload": {
            "owner_agent_id": "Elara",
            "owner_location": {"x": 0.0, "y": 0.0, "z": 0.0},
            "threat_level": "Low",
            "in_cover": False,
            "line_of_sight": False,
            "perceived_targets": []
        }
    })

    response_str = asyncio.run(_process_llm_message(raw_json))
    response = json.loads(response_str)

    assert response.get("status") == "dropped", f"예상: 'dropped', 실제: {response}"
    assert response.get("reason") == "stale_packet"
    print("[PASS] test_process_stale_packet_dropped")


# ═════════════════════════════════════════════════════════════════════════════
# 테스트 실행
# ═════════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    print("=" * 60)
    print("Envelope & Middleware 유닛 테스트 시작")
    print("=" * 60)

    test_valid_auth_token_passes()
    test_invalid_auth_token_rejected()
    test_stale_packet_detected()
    test_fresh_packet_passes()
    test_build_failed_event_structure()
    test_state_update_payload_parsing()
    test_prompt_payload_parsing()
    test_process_state_update_returns_cached()
    test_process_stale_packet_dropped()

    print("\n" + "=" * 60)
    print("✅ 모든 테스트 통과!")
    print("=" * 60)
