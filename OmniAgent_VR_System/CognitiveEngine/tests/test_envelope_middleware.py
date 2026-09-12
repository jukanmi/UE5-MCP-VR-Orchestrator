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
  [6] state_update Envelope 파싱 확인
  [7] prompt Envelope 파싱 확인
  [8] _process_message state_update → "cached" 응답 확인 (통합)
"""

import sys
import os
import time
import json
import asyncio

# 테스트용 WS_AUTH_TOKEN 환경변수 설정 (import 전에 반드시 먼저 설정)
os.environ["WS_AUTH_TOKEN"] = "test-token-for-unit-test"

from app.schemas.envelope import (
    MessageEnvelope,
    EEnvelopeType,
    StateUpdatePayload,
    PromptPayload,
)
from app.middleware import validate_auth_token, is_stale_packet


# ─────────────────────────────────────────────────────────────────────────────
# 테스트용 Envelope 팩토리 헬퍼
# WHY: 각 테스트에서 중복되는 Envelope 생성 코드를 함수로 추출.
# ─────────────────────────────────────────────────────────────────────────────


def make_prompt_envelope(token: str = "test-token-for-unit-test", timestamp_offset: float = 0.0) -> MessageEnvelope:
    """prompt 타입 테스트용 Envelope 생성."""
    return MessageEnvelope(
        msg_id="test-msg-001",
        auth_token=token,
        timestamp=time.time() - timestamp_offset,
        type=EEnvelopeType.PROMPT,
        payload={
            "player_id": "Player_01",
            "voice_transcript": "이리와!",
        },
    )


def make_state_update_envelope(
    token: str = "test-token-for-unit-test", timestamp_offset: float = 0.0
) -> MessageEnvelope:
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
                    "location": {"x": 100.0, "y": 200.0, "z": 0.0},
                }
            ],
        },
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
    raw_json = json.dumps(
        {
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
                "perceived_targets": [],
            },
        }
    )

    import traceback

    try:
        response_str = asyncio.run(_process_llm_message(raw_json))
        response = json.loads(response_str)

        assert response.get("status") == "cached", f"예상: 'cached', 실제: {response}"
        print("[PASS] test_process_state_update_returns_cached")
    except Exception:
        print("Exception in test_process_state_update_returns_cached:")
        traceback.print_exc()
        raise


def test_process_stale_packet_dropped():
    """Stale 패킷은 _process_llm_message에서 드랍 응답이 와야 한다."""
    from app.main import _process_llm_message

    raw_json = json.dumps(
        {
            "msg_id": "stale-msg",
            "auth_token": "test-token-for-unit-test",
            "type": "state_update",
            "timestamp": time.time() - 15.0,  # 15초 전 → Stale (임계 10초)
            "payload": {
                "owner_agent_id": "Elara",
                "owner_location": {"x": 0.0, "y": 0.0, "z": 0.0},
                "threat_level": "Low",
                "in_cover": False,
                "line_of_sight": False,
                "perceived_targets": [],
            },
        }
    )

    response_str = asyncio.run(_process_llm_message(raw_json))
    response = json.loads(response_str)

    assert response.get("status") == "dropped", f"예상: 'dropped', 실제: {response}"
    assert response.get("reason") == "stale_packet"
    print("[PASS] test_process_stale_packet_dropped")


def make_emergency_envelope(payload: dict) -> MessageEnvelope:
    """emergency_report 타입 테스트용 Envelope 생성."""
    return MessageEnvelope(
        msg_id="test-emergency-001",
        auth_token="test-token-for-unit-test",
        timestamp=time.time(),
        type=EEnvelopeType.EMERGENCY_REPORT,
        payload=payload,
    )


def _perception(target_id: str, danger: float) -> dict:
    return {
        "target_id": target_id,
        "sense_type": "Other",
        "distance": 0.0,
        "danger_score": danger,
        "location": {"x": 0.0, "y": 0.0, "z": 0.0},
    }


def test_emergency_report_type_default():
    """
    report_type 미포함 payload(기존 C++ perception 보고)는 "perception" 기본값으로
    파싱돼야 한다(하위호환). 명시 시 그대로 유지.
    """
    from app.schemas.envelope import EmergencyReportPayload

    legacy = EmergencyReportPayload(
        agent_id="Elara", perceptions=[_perception("Player_01", 0.9)], generated_at=time.time()
    )
    assert legacy.report_type == "perception"

    victory = EmergencyReportPayload(
        agent_id="Elara",
        perceptions=[_perception("Player_01", 0.0)],
        generated_at=time.time(),
        report_type="combat_victory",
    )
    assert victory.report_type == "combat_victory"
    print("[PASS] test_emergency_report_type_default")


def test_combat_victory_routed_no_action():
    """
    combat_victory 보고는 danger 게이트에 걸리지 않고(danger=0 이어도) 승리 분기로
    라우팅돼야 한다: 무행동 배치(Common, ActionBatches 빈) 반환 + 메모리 Event 기록.
    SLM/DB 는 건드리지 않는다.
    """
    from app import main as main_module

    recorded = []

    class _FakeMemory:
        def add_entry(self, speaker, content):
            recorded.append((speaker, content))

    # 실 메모리 파일 쓰기 방지 — main 이 get_memory 를 모듈 상단에서 바인딩하므로 main 쪽 이름을 패치.
    original_get_memory = main_module.get_memory
    main_module.get_memory = lambda agent_id: _FakeMemory()
    try:
        envelope = make_emergency_envelope(
            {
                "agent_id": "Elara",
                "perceptions": [_perception("Player_01", 0.0)],
                "generated_at": time.time(),
                "report_type": "combat_victory",
            }
        )

        async def _run():
            resp = await main_module._handle_emergency_report(envelope)
            # fire-and-forget 메모리 기록 태스크 완료까지 대기 후 검증 (R4: 공용 헬퍼로 이동)
            from app.utils.async_tasks import _background_tasks

            if _background_tasks:
                await asyncio.gather(*list(_background_tasks))
            return resp

        response = json.loads(asyncio.run(_run()))
    finally:
        main_module.get_memory = original_get_memory

    assert response.get("Mode") == "Common", f"예상: Common, 실제: {response}"
    assert response.get("ActionBatches") == {}, f"무행동 기대, 실제: {response}"
    assert recorded, "승리 메모리 기록이 없음"
    assert recorded[0][0] == "Event" and "Player_01" in recorded[0][1]
    print("[PASS] test_combat_victory_routed_no_action")


def test_emergency_low_danger_still_gated():
    """
    report_type 미포함(기본 perception) 저위협 보고는 기존과 동일하게 danger 게이트에서
    무행동 드랍돼야 한다 — report_type 추가가 기존 경로를 바꾸지 않는지 회귀 확인.
    """
    from app import main as main_module

    envelope = make_emergency_envelope(
        {
            "agent_id": "Elara",
            "perceptions": [_perception("Player_01", 0.2)],
            "generated_at": time.time(),
        }
    )

    response = json.loads(asyncio.run(main_module._handle_emergency_report(envelope)))

    assert response.get("Mode") == "Common"
    assert response.get("ActionBatches") == {}
    print("[PASS] test_emergency_low_danger_still_gated")


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
    test_state_update_payload_parsing()
    test_prompt_payload_parsing()
    test_process_state_update_returns_cached()
    test_emergency_report_type_default()
    test_combat_victory_routed_no_action()
    test_emergency_low_danger_still_gated()
    test_process_stale_packet_dropped()

    print("\n" + "=" * 60)
    print("✅ 모든 테스트 통과!")
    print("=" * 60)
