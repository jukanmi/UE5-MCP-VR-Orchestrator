"""

 File: middleware.py                                                         
 Role: 메시지 수신 후 처리 전 실행되는 보안 및 무결성 검증 계층              

 WHY (설계 의도):                                                             
   main.py의 WebSocket 핸들러가 직접 모든 검증 로직을 담으면                
   코드가 비대해지고 테스트하기 어려워진다.                                  
   이 파일은 "게이트키퍼" 역할로, 메시지가 파이프라인에 진입하기 전         
   보안(인증), 무결성(타임스탬프), 이력(실패 기록) 검증을 전담한다.         
                                                                              
 책임:                                                                       
   1. validate_auth_token   : Bearer 토큰이 환경변수와 일치하는지 확인      
   2. is_stale_packet        : 오래된 패킷(Race Condition 원인)을 차단       
   3. build_failed_event     : action_failed 이력 기록용 딕셔너리 생성      

"""
import os
import time
import logging
from typing import Dict, Any, Optional

from .schemas.envelope import MessageEnvelope, ActionFailedPayload

logger = logging.getLogger(__name__)


# ─────────────────────────────────────────────────────────────────────────────
# WHY: 환경변수는 모듈 로드 시 1회만 읽는다.
#      매 요청마다 os.getenv()를 호출하면 성능 낭비이며,
#      서버 실행 중 토큰이 변경되어도 재시작 전까지 반영되지 않아 일관성이 유지된다.
# ─────────────────────────────────────────────────────────────────────────────
_EXPECTED_AUTH_TOKEN: Optional[str] = os.getenv("WS_AUTH_TOKEN")

if not _EXPECTED_AUTH_TOKEN:
    logger.warning(
        "[Middleware] WS_AUTH_TOKEN이 환경변수에 설정되지 않았습니다. "
        "모든 인증 토큰이 거부됩니다. .env 파일을 확인하세요."
    )


def validate_auth_token(token: str) -> bool:
    """
    수신된 auth_token이 서버의 기대값과 일치하는지 검증한다.

    WHY: WebSocket 핸드셰이크만으로는 부족하다.
         연결이 탈취되거나 재사용될 경우, 개별 메시지 단위 인증으로
         비인가 요청이 파이프라인에 진입하는 것을 막아야 한다.

    Args:
        token: MessageEnvelope에서 꺼낸 auth_token 문자열

    Returns:
        True  = 인증 성공 (파이프라인 진입 허용)
        False = 인증 실패 (즉시 드랍)
    """
    if not _EXPECTED_AUTH_TOKEN:
        # 토큰 미설정 = 보안 정책 오류 → 모든 요청 차단
        logger.error("[Auth] WS_AUTH_TOKEN 미설정으로 인해 모든 요청이 거부됩니다.")
        return False

    import hmac
    is_valid = hmac.compare_digest(token, _EXPECTED_AUTH_TOKEN)
    if not is_valid:
        logger.warning(f"[Auth] 잘못된 auth_token: '{token[:8]}...' (8자리 이후 생략)")
    return is_valid


def is_stale_packet(packet_timestamp: float, threshold_seconds: float = 2.0) -> bool:
    """
    패킷의 timestamp가 현재 시각보다 threshold_seconds 이상 오래되었는지 확인한다.

    WHY: UE5와 Python 사이 네트워크 지연이나 큐 쌓임으로 인해
         오래된 상태(Stale State)가 LLM에 입력되면 잘못된 판단을 내릴 수 있다.
         Race Condition을 원천 차단하기 위해 고정된 시간 창(Window)을 초과한
         패킷은 무조건 드랍한다.

    Args:
        packet_timestamp   : 메시지의 생성 시각 (Unix epoch seconds)
        threshold_seconds  : 허용 지연 임계값. 기본 2초.

    Returns:
        True  = 오래된 패킷 (드랍 대상)
        False = 유효한 패킷 (처리 가능)
    """
    current_time = time.time()
    age_seconds = current_time - packet_timestamp

    if age_seconds > threshold_seconds:
        logger.warning(
            f"[Middleware] Stale 패킷 감지: {age_seconds:.2f}초 지연 "
            f"(임계값: {threshold_seconds}초). 드랍합니다."
        )
        return True
    return False


def build_failed_event(envelope: MessageEnvelope) -> Dict[str, Any]:
    """
    action_failed 메시지를 AgentState.failed_action_history 항목으로 변환한다.

    WHY: Python이 LLM을 통해 내린 명령이 UE5에서 실패했다면,
         그 실패 이유를 다음 추론 때 컨텍스트로 제공해야 동일 실수를 반복하지 않는다.
         ref_msg_id는 "어떤 명령이 실패했는가"를 추적하는 핵심 키다.

    Args:
        envelope: action_failed 타입의 MessageEnvelope

    Returns:
        이력 딕셔너리:
          {
            "ref_msg_id": 실패한 원본 명령의 msg_id,
            "failed_action_type": 실패한 액션 종류,
            "reason": 실패 이유,
            "executor_npc_id": 명령을 수행하려 했던 NPC ID,
            "recorded_at": 기록 시각
          }
    """
    try:
        failed_payload: ActionFailedPayload = envelope.parse_action_failed_payload()
    except Exception as parse_error:
        logger.error(f"[Middleware] action_failed payload 파싱 실패: {parse_error}")
        # 파싱 실패 시에도 기본 정보는 기록하여 이력을 유지한다
        return {
            "ref_msg_id": envelope.ref_msg_id,
            "failed_action_type": "Unknown",
            "reason": f"Payload 파싱 오류: {parse_error}",
            "executor_npc_id": "Unknown",
            "recorded_at": time.time(),
        }

    return {
        "ref_msg_id": envelope.ref_msg_id,
        "failed_action_type": failed_payload.failed_action_type,
        "reason": failed_payload.reason,
        "executor_npc_id": failed_payload.executor_npc_id,
        "recorded_at": time.time(),
    }
