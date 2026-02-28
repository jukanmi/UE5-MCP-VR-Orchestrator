"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: main.py                                                               ║
║ Role: FASTAPI ENTRY POINT (UE5 ↔ Cognitive Engine Bridge)                  ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Manage WebSocket connection with Unreal Engine 5.                         ║
║   모든 수신 메시지는 MessageEnvelope로 먼저 파싱된 후 타입에 따라 분기.    ║
║                                                                              ║
║ MESSAGE TYPE 처리 흐름:                                                     ║
║   • state_update  → 상태 캐시 갱신만 (LLM 미호출) → {"status": "cached"}  ║
║   • prompt        → LangGraph 파이프라인 실행 → ActionBatch 반환           ║
║   • action_failed → 실패 이력 기록만 (LLM 미호출) → {"status": "logged"}  ║
║                                                                              ║
║ 보안 게이트 (미들웨어 단계):                                                ║
║   1. auth_token 검증 → 실패 시 즉시 종료                                   ║
║   2. Stale 패킷 감지 (2초 초과) → 실패 시 즉시 드랍                        ║
║   3. Pydantic 스키마 검증 → 실패 시 에러 응답                              ║
║                                                                              ║
║ ENDPOINTS:                                                                   ║
║   • GET  /       - 헬스체크                                                 ║
║   • WS   /ws/ue5 - UE5 메인 WebSocket 엔드포인트                           ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import json
import logging
import traceback

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import ValidationError
from typing import Optional

from .schemas.envelope import MessageEnvelope, EEnvelopeType, PromptPayload
from .schemas.vr_context import GesPrompt, GestureData
from .schemas.actions import ActionBatch
from .agents.state import AgentState
from .graph import app_graph
from .middleware import validate_auth_token, is_stale_packet, build_failed_event

logger = logging.getLogger(__name__)

app = FastAPI()

# ─────────────────────────────────────────────────────────────────────────────
# 연결당 공유 상태 (서버 재시작 전까지 유지)
# WHY: state_update로 캐시된 월드 상태와 누적된 실패 이력은
#      연결이 유지되는 동안 지속되어야 다음 prompt 처리 시 활용할 수 있다.
#      현재는 단일 연결 기준으로 단순화. 멀티 클라이언트 확장 시 Dict[client_id, ...]로 변경.
# ─────────────────────────────────────────────────────────────────────────────
_cached_world_state: Optional[dict] = None
_failed_action_history: list = []


@app.get("/")
async def health_check():
    """서버 정상 동작 여부를 확인하는 헬스체크 엔드포인트."""
    return {"message": "OmniAgent Cognitive Engine is running"}


@app.websocket("/ws/ue5")
async def websocket_ue5_endpoint(websocket: WebSocket):
    """
    UE5와의 메인 WebSocket 엔드포인트.

    WHY 개별 핸들러 분리:
      메시지 타입별 처리 로직을 별도 함수로 분리하여
      각 핸들러를 독립적으로 테스트하고 유지보수할 수 있게 한다.
    """
    await websocket.accept()
    logger.info("[Main] UE5 클라이언트 연결됨")

    try:
        while True:
            raw_data = await websocket.receive_text()
            response = await _process_message(raw_data)
            await websocket.send_text(response)

    except WebSocketDisconnect:
        logger.info("[Main] UE5 클라이언트 연결 종료")


async def _process_message(raw_data: str) -> str:
    """
    수신된 원시 JSON 문자열을 처리하고 응답 JSON 문자열을 반환한다.

    WHY 이 함수를 별도로 분리:
      WebSocket 바인딩 코드(accept, receive, send)와 실제 비즈니스 로직을 분리하여
      유닛 테스트에서 WebSocket 없이도 처리 로직만 검증할 수 있게 한다.
    """
    try:
        # ── Step 1: Envelope 파싱 ─────────────────────────────────────
        raw_json = json.loads(raw_data)
        envelope = MessageEnvelope(**raw_json)

        # ── Step 2: 인증 검증 ─────────────────────────────────────────
        # WHY: 잘못된 토큰이면 파이프라인 자체에 진입시키지 않는다.
        if not validate_auth_token(envelope.auth_token):
            logger.warning(f"[Main] 인증 실패. msg_id={envelope.msg_id}")
            return json.dumps({"error": "Unauthorized", "msg_id": envelope.msg_id})

        # ── Step 3: Stale 패킷 감지 ───────────────────────────────────
        # WHY: 오래된 상태 정보가 LLM에 입력되면 잘못된 판단을 유발한다.
        if is_stale_packet(envelope.timestamp, threshold_seconds=2.0):
            logger.warning(f"[Main] Stale 패킷 드랍. msg_id={envelope.msg_id}")
            return json.dumps({"status": "dropped", "reason": "stale_packet", "msg_id": envelope.msg_id})

        # ── Step 4: 타입별 분기 ───────────────────────────────────────
        if envelope.type == EEnvelopeType.PROMPT:
            return await _handle_prompt(envelope)

        elif envelope.type == EEnvelopeType.STATE_UPDATE:
            return _handle_state_update(envelope)

        elif envelope.type == EEnvelopeType.ACTION_FAILED:
            return _handle_action_failed(envelope)

        else:
            logger.error(f"[Main] 알 수 없는 메시지 타입: {envelope.type}")
            return json.dumps({"error": f"Unknown message type: {envelope.type}"})

    except ValidationError as ve:
        logger.error(f"[Main] Envelope 스키마 검증 실패: {ve}")
        return json.dumps({"error": "Schema validation failed", "detail": str(ve)})

    except json.JSONDecodeError as je:
        logger.error(f"[Main] JSON 파싱 실패: {je}")
        return json.dumps({"error": "Invalid JSON format"})

    except Exception as e:
        logger.error(f"[Main] 처리 중 예외 발생: {e}")
        traceback.print_exc()
        return json.dumps({"error": str(e)})


async def _handle_prompt(envelope: MessageEnvelope) -> str:
    """
    prompt 타입 처리: LangGraph 파이프라인을 실행하여 ActionBatch를 생성한다.

    WHY payload를 GesPrompt로 변환:
      기존 파이프라인(Interface Input, Dialogue 등)이 GesPrompt를 입력으로 받으므로,
      Envelope의 PromptPayload를 GesPrompt로 변환하여 하위 호환성을 유지한다.
    """
    global _cached_world_state, _failed_action_history

    logger.info(f"[Main] prompt 처리 시작. msg_id={envelope.msg_id}")

    prompt_payload: PromptPayload = envelope.parse_prompt_payload()

    # PromptPayload → GesPrompt 변환 (기존 파이프라인 호환)
    ges_prompt = GesPrompt(
        player_id=prompt_payload.player_id,
        voice_transcript=prompt_payload.voice_transcript,
        gestures=[GestureData(**g) for g in prompt_payload.gestures] if prompt_payload.gestures else [],
        timestamp=envelope.timestamp,
        last_event=prompt_payload.last_event,
        stats=prompt_payload.stats,
        looking_at_entity_id=prompt_payload.looking_at_entity_id,
        player_location=prompt_payload.player_location,
    )

    # 초기 AgentState 구성
    # WHY cached_world_state와 failed_action_history를 주입:
    #   이전 state_update/action_failed에서 축적된 컨텍스트를
    #   현재 LLM 추론에 함께 제공하여 판단 품질을 높인다.
    initial_state: AgentState = AgentState(
        messages=[],
        vr_context=ges_prompt,
        cached_world_state=_cached_world_state,
        failed_action_history=list(_failed_action_history),  # 복사본 전달
        next="",
        current_speaker="",
        natural_context=None,
        raw_response=None,
        target_npc=None,
        behavior_mode=None,
        facial_state=None,
        action_batch=None,
        # 보안 및 라우팅 가드레일 초기값
        target_npcs=[],
        msg_id=envelope.msg_id,
        timestamp=envelope.timestamp,
        has_error=False,
        error_msg=None,
    )

    # [Issue 2 Fix] 복사본을 AgentState에 넘겼으므로 원본 누적 이력 비우기
    # WHY: append만 하고 clear가 없으면 서버 장기 운영 시 RAM 초과(OOM) 위험
    _failed_action_history.clear()

    logger.info("[Main] Graph 비동기 실행 시작...")
    # [Issue 1 Fix] 동기 invoke() → 비동기 ainvoke()로 이벤트 루프 해방
    # WHY: invoke()는 LLM 추론 2~3초간 이벤트 루프를 블로킹하여
    #       다른 NPC의 state_update 수신이 중단되는 치명적 문제 발생
    try:
        result = await app_graph.ainvoke(initial_state)
    except Exception as e:
        logger.error(f"[Main] LangGraph 실행 중 치명적 오류: {e}")
        traceback.print_exc()
        return json.dumps({
            "agent_id": "System",
            "actions": [],
            "reasoning": f"Graph execution error: {e}",
        })

    final_action: Optional[ActionBatch] = result.get("action_batch")

    if final_action:
        logger.info(f"[Main] ActionBatch 생성 완료: agent_id={final_action.agent_id}")
        return final_action.model_dump_json()
    else:
        logger.warning("[Main] 에이전트가 ActionBatch를 생성하지 않았습니다.")
        return json.dumps({
            "agent_id": "System",
            "actions": [],
            "reasoning": "No action generated by agents.",
        })


def _handle_state_update(envelope: MessageEnvelope) -> str:
    """
    state_update 타입 처리: LLM 호출 없이 월드 상태만 캐시한다.

    WHY LLM을 호출하지 않는가:
      state_update는 주기적으로 도착하는 '현재 상태 스냅샷'이다.
      매번 LLM을 호출하면 비용과 지연이 폭발적으로 증가한다.
      대신 캐시해 두었다가, 다음 prompt 처리 시 컨텍스트로 활용한다.
    """
    global _cached_world_state

    try:
        state_payload = envelope.parse_state_update_payload()
        _cached_world_state = state_payload.model_dump()
        logger.info(f"[Main] 월드 상태 캐시 갱신 완료. msg_id={envelope.msg_id}, "
                    f"threat_level={state_payload.threat_level}")
        return json.dumps({"status": "cached", "msg_id": envelope.msg_id})

    except Exception as e:
        logger.error(f"[Main] state_update 파싱 실패: {e}")
        return json.dumps({"error": f"state_update parse error: {e}"})


def _handle_action_failed(envelope: MessageEnvelope) -> str:
    """
    action_failed 타입 처리: 실패 이력을 기록한다. LLM 미호출.

    WHY ref_msg_id가 핵심인가:
      ref_msg_id는 "어떤 명령"이 실패했는지 추적하는 키이다.
      이 이력을 누적하여 다음 LLM 추론 시 인풋으로 제공함으로써
      "Move가 PathNotFound로 실패했다"는 사실을 AI가 인식하고
      대안 행동(Wait, Dialogue 등)을 선택하도록 유도한다.
    """
    global _failed_action_history

    failed_event = build_failed_event(envelope)
    _failed_action_history.append(failed_event)

    logger.warning(
        f"[Main] 명령 실패 이력 기록. ref_msg_id={envelope.ref_msg_id}, "
        f"action={failed_event.get('failed_action_type')}, "
        f"reason={failed_event.get('reason')}"
    )

    return json.dumps({"status": "logged", "msg_id": envelope.msg_id})
