import json
import logging
import traceback
import asyncio

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import ValidationError
from typing import Optional
from contextlib import asynccontextmanager

from .schemas.envelope import MessageEnvelope, EEnvelopeType, PromptPayload, LocationDecisionPayload
from .schemas.vr_context import GesPrompt, GestureData
from .schemas.actions import ActionBatch, ModeActionRequest
from .agents.state import AgentState
from .graph import app_graph
from .utils import db_manager
from .middleware import validate_auth_token, is_stale_packet

logger = logging.getLogger("api")
logger.setLevel(logging.INFO)

@asynccontextmanager
async def lifespan(app: FastAPI):
    # --- Startup ---
    await db_manager.init_db()
    await db_manager.start_background_sync()
    yield
    # --- Shutdown ---
    await db_manager.stop_background_sync()

app = FastAPI(lifespan=lifespan)


_cached_world_state: Optional[dict] = None
_failed_action_history: list = []


@app.get("/")
async def health_check():
    return {"message": "OmniAgent Cognitive Engine is running"}


@app.websocket("/ws/llm")
async def websocket_llm_endpoint(websocket: WebSocket):
    await websocket.accept()
    logger.info("[Main] UE5 LLM 클라이언트 연결됨")

    try:
        while True:
            raw_data = await websocket.receive_text()
            response = await _process_llm_message(raw_data)
            await websocket.send_text(response)

    except WebSocketDisconnect:
        logger.info("[Main] UE5 LLM 클라이언트 연결 종료")

async def _process_llm_message(raw_data: str) -> str:
    try:
        # ── Step 1: Envelope 파싱 ─────────────────────────────────────
        raw_json = json.loads(raw_data)
        envelope = MessageEnvelope(**raw_json)

        # ── Step 2: 인증 검증 ─────────────────────────────────────────
        if not validate_auth_token(envelope.auth_token):
            logger.warning(f"[Main] 인증 실패. msg_id={envelope.msg_id}")
            return json.dumps({"error": "Unauthorized", "msg_id": envelope.msg_id})

        # ── Step 3: Stale 패킷 감지 ───────────────────────────────────
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
            
        elif envelope.type == EEnvelopeType.EMERGENCY_REPORT:
            return await _handle_emergency_report(envelope)

        elif envelope.type == EEnvelopeType.LOCATION_DECISION:
            return await _handle_location_decision(envelope)

        else:
            logger.error(f"[Main] 알 수 없는 메시지 타입: {envelope.type}")
            return json.dumps({"error": f"Unknown message type: {envelope.type}"})

    except ValidationError as ve:
        logger.error(f"[Main] Envelope 스키마 검증 실패: \n{traceback.format_exc()}")
        return json.dumps({"error": "Schema validation failed", "detail": str(ve)})

    except json.JSONDecodeError as je:
        logger.error(f"[Main] JSON 파싱 실패: \n{traceback.format_exc()}")
        return json.dumps({"error": "Invalid JSON format"})

    except Exception as e:
        logger.error(f"[Main] 예기치 않은 오류: \n{traceback.format_exc()}")
        return json.dumps({"error": "Internal server error", "detail": str(e)})

@app.websocket("/ws/slm")
async def websocket_slm_endpoint(websocket: WebSocket):
    await websocket.accept()
    logger.info("[Main] UE5 SLM 클라이언트 연결됨")

    try:
        while True:
            raw_data = await websocket.receive_text()
            response = await _process_slm_message(raw_data)
            await websocket.send_text(response)

    except WebSocketDisconnect:
        logger.info("[Main] UE5 SLM 클라이언트 연결 종료")


async def _process_slm_message(raw_data: str) -> str:
    try:
        # TODO: Step 1: 원시 데이터 수신
        # TODO: Step 2: Ollama SLM 비동기 호출
        # TODO: Step 3: UE5로 보낼 응답 양식 확정 후 구현
        return '{"status": "TODO: SLM Response Format"}'

    except Exception as e:
        logger.error(f"[Main] SLM 처리 중 오류 발생: {e}")
        import traceback
        traceback.print_exc()
        return json.dumps({"error": str(e)})

async def _handle_emergency_report(envelope: MessageEnvelope) -> str:
    global _cached_world_state, _failed_action_history

    try:
        payload = envelope.parse_emergency_report_payload()
        logger.info(f"[Main] 긴급 보고 수신. npc={payload.agent_id}, perceptions={len(payload.perceptions)}")

        # 1. Perception 요약 텍스트 생성
        lines = []
        for p in payload.perceptions:
            lines.append(f"- Detected '{p.target_id}' via {p.sense_type} at dist {p.distance:.1f} (Danger: {p.danger_score:.2f})")
        
        system_transcript = "[SYSTEM ALERT] I perceived the following events:\n" + "\n".join(lines) + "\nHow should I react immediately?"

        # 2. 합성된 GesPrompt 생성 (System 프롬프트로 우회)
        ges_prompt = GesPrompt(
            player_id="System",
            voice_transcript=system_transcript,
            gestures=[],
            timestamp=envelope.timestamp,
            last_event="EmergencyEventDetected",
            stats=None,
            looking_at_entity_id=None,
            player_location=None,
        )

        initial_state: AgentState = AgentState(
            messages=[],
            vr_context=ges_prompt,
            cached_world_state=_cached_world_state,
            failed_action_history=list(_failed_action_history),
            next="",
            current_speaker="",
            natural_context=None,
            raw_response=None,
            target_npc=payload.agent_id, # 이벤트를 감지한 해당 NPC가 행동하도록 지정
            behavior_mode=None,
            facial_state=None,
            action_batch=None,
            target_npcs=[],
            msg_id=envelope.msg_id,
            timestamp=envelope.timestamp,
            has_error=False,
            error_msg=None,
        )

        logger.info(f"[Main] 긴급 이벤트로 인한 Graph 비동기 실행 시작... (Target NPC: {payload.agent_id})")
        
        result = await app_graph.ainvoke(initial_state)
        
        final_action: Optional[ActionBatch] = result.get("action_batch")

        if final_action:
            logger.info(f"[Main] Emergency ActionBatch 생성 완료: agent_id={final_action.AgentID}")
            wrapper = ModeActionRequest(
                Mode=final_action.Mode,
                ActionBatches={final_action.AgentID: final_action}
            )
            return wrapper.model_dump_json()
        else:
            logger.warning("[Main] 에이전트가 긴급 상황에 대한 ActionBatch를 생성하지 않았습니다.")
            fallback = ModeActionRequest(Mode="Common", ActionBatches={})
            return fallback.model_dump_json()

    except Exception as e:
        logger.error(f"[Main] _handle_emergency_report 실행 중 치명적 오류: {e}")
        import traceback
        traceback.print_exc()
        fallback = ModeActionRequest(Mode="Common", ActionBatches={})
        return fallback.model_dump_json()


async def _handle_prompt(envelope: MessageEnvelope) -> str:
    global _cached_world_state, _failed_action_history

    logger.info(f"[Main] prompt 처리 시작. msg_id={envelope.msg_id}")

    prompt_payload: PromptPayload = envelope.parse_prompt_payload()

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

    initial_state: AgentState = AgentState(
        messages=[],
        vr_context=ges_prompt,
        cached_world_state=_cached_world_state,
        failed_action_history=list(_failed_action_history),
        next="",
        current_speaker="",
        natural_context=None,
        raw_response=None,
        target_npc=None,
        behavior_mode=None,
        facial_state=None,
        action_batch=None,
        target_npcs=[],
        msg_id=envelope.msg_id,
        timestamp=envelope.timestamp,
        has_error=False,
        error_msg=None,
    )

    _failed_action_history.clear()

    logger.info("[Main] Graph 비동기 실행 시작...")
    try:
        result = await app_graph.ainvoke(initial_state)
    except Exception as e:
        logger.error(f"[Main] LangGraph 실행 중 치명적 오류: {e}")
        traceback.print_exc()
        fallback = ModeActionRequest(Mode="Common", ActionBatches={})
        return fallback.model_dump_json()

    final_action: Optional[ActionBatch] = result.get("action_batch")

    if final_action:
        logger.info(f"[Main] ActionBatch 생성 완료: agent_id={final_action.AgentID}")
        wrapper = ModeActionRequest(
            Mode=final_action.Mode,
            ActionBatches={final_action.AgentID: final_action}
        )
        return wrapper.model_dump_json()
    else:
        logger.warning("[Main] 에이전트가 ActionBatch를 생성하지 않았습니다.")
        fallback = ModeActionRequest(Mode="Common", ActionBatches={})
        return fallback.model_dump_json()



def _handle_state_update(envelope: MessageEnvelope) -> str:
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


async def _handle_location_decision(envelope: MessageEnvelope) -> str:
    """
    location_decision 핸들러.
    WHY: C++이 EQS로 후보를 뽑고 스코어링까지 완료한 뒤 최종 카테고리 선택만
         LLM에 위임한다. 전체 좌표 생성 없이 경량 판단만 수행하므로 latency가 짧다.

    응답 형식:
        { "type": "location_decision_result",
          "payload": { "agent_id": "...", "chosen_id": "SAFE_0", "reason": "..." } }
    """
    try:
        payload = LocationDecisionPayload(**(envelope.payload if isinstance(envelope.payload, dict) else dict(envelope.payload)))
        logger.info(f"[Main] location_decision 수신: agent={payload.agent_id}, "
                    f"candidates={len(payload.candidates)}")

        if not payload.candidates:
            raise ValueError("No location candidates provided by UE5.")

        best_candidate = max(payload.candidates, key=lambda c: c.score)
        chosen_id = best_candidate.id
        reason = f"Fast-Path: Selected highest score ({best_candidate.score:.2f}) from EQS"

        logger.info(f"[Main] location_decision 결과(Fast-Path): chosen={chosen_id} reason={reason}")

        return json.dumps({
            "type": "location_decision_result",
            "payload": {
                "agent_id": payload.agent_id,
                "chosen_id": chosen_id,
                "reason": reason,
            }
        })

    except Exception as e:
        logger.error(f"[Main] _handle_location_decision 오류: {e}")
        traceback.print_exc()
        # 빈 응답 대신 첫 번째 후보로 폴백
        try:
            payload_raw = envelope.payload if isinstance(envelope.payload, dict) else {}
            agent_id = payload_raw.get("agent_id", "unknown")
            candidates = payload_raw.get("candidates", [])
            fallback_id = candidates[0]["id"] if candidates else "OPTIMAL_0"
            return json.dumps({
                "type": "location_decision_result",
                "payload": {"agent_id": agent_id, "chosen_id": fallback_id, "reason": "error_fallback"}
            })
        except Exception:
            return json.dumps({"error": "location_decision failed"})


def _handle_action_failed(envelope: MessageEnvelope) -> str:
    global _failed_action_history

    failed_event = build_failed_event(envelope)
    _failed_action_history.append(failed_event)

    logger.warning(
        f"[Main] 명령 실패 이력 기록. ref_msg_id={envelope.ref_msg_id}, "
        f"action={failed_event.get('failed_action_type')}, "
        f"reason={failed_event.get('reason')}"
    )

    return json.dumps({"status": "logged", "msg_id": envelope.msg_id})
