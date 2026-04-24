import json
import logging
import traceback
import asyncio

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import ValidationError
from typing import Optional
from contextlib import asynccontextmanager

from .schemas.envelope import MessageEnvelope, EEnvelopeType, PromptPayload, LocationDecisionPayload, EmergencyReportPayload
from .schemas.vr_context import GesPrompt, GestureData
from .schemas.actions import ActionBatch, ModeActionRequest
from .agents.state import AgentState
from .graph import app_graph
from .utils import db_manager
from .middleware import validate_auth_token, is_stale_packet, build_failed_event

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
        raw_json = json.loads(raw_data)
        envelope = MessageEnvelope(**raw_json)

        if not validate_auth_token(envelope.auth_token):
            return json.dumps({"error": "Unauthorized", "msg_id": envelope.msg_id})

        if is_stale_packet(envelope.timestamp, threshold_seconds=1.0):
            return json.dumps({"status": "dropped", "reason": "stale_packet", "msg_id": envelope.msg_id})

        if envelope.type != EEnvelopeType.EMERGENCY_REPORT:
            return json.dumps({"error": f"SLM only handles emergency_report, got: {envelope.type}"})

        payload = envelope.parse_emergency_report_payload()
        return await _handle_slm_reflex(payload)

    except ValidationError as ve:
        logger.error(f"[SLM] Envelope 검증 실패: {ve}")
        return json.dumps({"error": "Schema validation failed"})
    except json.JSONDecodeError:
        return json.dumps({"error": "Invalid JSON format"})
    except Exception as e:
        logger.error(f"[SLM] 처리 오류: {e}")
        traceback.print_exc()
        return json.dumps({"error": str(e)})

_REFLEX_FACIAL: dict = {
    "Attack": "Angry", "Block": "Fear", "Dodge": "Surprised",
    "Flee": "Fear", "SignalAllies": "Surprised", "Scan": "Surprised",
}

_REFLEX_PROMPT = """\
NPC '{agent_id}' detects a threat:
- Target: {target_id}, Sense: {sense}, Distance: {dist:.1f}m, Danger: {danger:.2f}
{extra_lines}
Choose ONE immediate action: Attack, Block, Dodge, Flee, SignalAllies, Scan
Reply with ONLY the action name, e.g.: Attack"""


async def _handle_slm_reflex(payload: EmergencyReportPayload) -> str:
    """
    SLM 반사 행동 결정 (목표 500ms).
    LangGraph 없이 단일 경량 SLM 호출로 즉각 전투/회피 액션 생성.
    """
    from .utils.llm_factory import get_llm
    from .schemas.actions import ActionBatch, GameAction, ModeActionRequest

    agent_id = payload.agent_id
    perceptions = payload.perceptions

    if not perceptions:
        batch = ActionBatch(AgentID=agent_id, Mode="Combat", Actions=[
            GameAction(ActionType="Scan", FacialState="Surprised", Parameters={})
        ])
        return ModeActionRequest(Mode="Combat", ActionBatches={agent_id: batch}).model_dump_json()

    top = max(perceptions, key=lambda p: p.danger_score)

    extra_lines = ""
    if len(perceptions) > 1:
        others = [f"  - {p.target_id} ({p.sense_type}, dist {p.distance:.1f})" for p in perceptions[1:3]]
        extra_lines = "Other threats:\n" + "\n".join(others)

    prompt = _REFLEX_PROMPT.format(
        agent_id=agent_id,
        target_id=top.target_id,
        sense=top.sense_type,
        dist=top.distance,
        danger=top.danger_score,
        extra_lines=extra_lines,
    )

    action_type = "Scan"
    try:
        llm = get_llm(model_name="gemma4_slm", temperature=0.0, num_predict=10)
        raw = await asyncio.to_thread(llm.invoke, prompt)
        text = (raw.content if hasattr(raw, "content") else str(raw)).strip().split()[0]

        valid = {"Attack", "Block", "Dodge", "Flee", "SignalAllies", "Scan"}
        if text.capitalize() in valid:
            action_type = text.capitalize()
        else:
            # 키워드 검색 폴백
            text_l = text.lower()
            for a in valid:
                if a.lower() in text_l:
                    action_type = a
                    break

    except Exception as e:
        logger.error(f"[SLM] 추론 실패, 위험도 기반 폴백 사용: {e}")
        action_type = "Attack" if top.danger_score >= 0.85 else "Flee"

    params: dict = {}
    if action_type == "Attack":
        params["target_id"] = top.target_id
    elif action_type in {"Move", "Flee"}:
        params["style"] = "Run"

    facial = _REFLEX_FACIAL.get(action_type, "Neutral")
    batch = ActionBatch(
        AgentID=agent_id,
        Mode="Combat",
        Actions=[GameAction(ActionType=action_type, FacialState=facial, Parameters=params)]
    )
    result = ModeActionRequest(Mode="Combat", ActionBatches={agent_id: batch})

    logger.info(f"[SLM] Reflex: {agent_id} → {action_type} (danger={top.danger_score:.2f})")
    return result.model_dump_json()


async def _handle_emergency_report(envelope: MessageEnvelope) -> str:
    try:
        payload = envelope.parse_emergency_report_payload()
        logger.info(f"[Main] 긴급 보고 수신. npc={payload.agent_id}, perceptions={len(payload.perceptions)}")

        # ── [핵심 최적화] 긴급 전투 상황의 0.5초 반사 신경(Reflex) 라우팅 ──
        # 기존에는 무거운 LangGraph 파이프라인(26B 모델)을 전체 순회하여
        # 수 초간의 지연이 발생했으나, 이제는 즉시 4B 경량 SLM 또는 키워드 폴백으로
        # 우회 처리(Bypass)하여 체감 지연 시간을 제로에 가깝게 최적화함.
        logger.info(f"[Main] LangGraph 우회: {payload.agent_id}의 긴급 상황을 SLM Reflex로 즉시 처리합니다.")
        return await _handle_slm_reflex(payload)

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

        # 해당 NPC의 관계 데이터를 캐시에서 읽어 응답에 포함
        relations = db_manager.get_relations_from_cache(state_payload.owner_agent_id)

        return json.dumps({
            "status": "cached",
            "msg_id": envelope.msg_id,
            "agent_id": state_payload.owner_agent_id,
            "relations": relations,
        })

    except Exception as e:
        logger.error(f"[Main] state_update 파싱 실패: {e}")
        return json.dumps({"error": f"state_update parse error: {e}"})


_LOCATION_DECISION_PROMPT = """\
NPC '{agent_id}' must choose a tactical position.
Situation: {context}

Candidates:
{candidate_lines}

Rules:
- SAFE positions: prefer when HP is low or outnumbered
- OPTIMAL positions: balanced engagement range and cover
- AGGRESSIVE positions: prefer when HP is high and enemy is isolated

Reply with ONLY the candidate ID (e.g. SAFE_0). Nothing else."""


async def _handle_location_decision(envelope: MessageEnvelope) -> str:
    """
    location_decision 핸들러.
    WHY: C++이 EQS로 후보를 뽑고 스코어링까지 완료한 뒤 최종 카테고리 선택만
         LLM에 위임한다. 전체 좌표 생성 없이 경량 판단만 수행하므로 latency가 짧다.

    응답 형식:
        { "type": "location_decision_result",
          "payload": { "agent_id": "...", "chosen_id": "SAFE_0", "reason": "..." } }
    """
    from .utils.llm_factory import get_llm

    payload_raw = envelope.payload if isinstance(envelope.payload, dict) else {}
    agent_id = payload_raw.get("agent_id", "unknown")
    candidates_raw = payload_raw.get("candidates", [])

    def _fast_path_fallback(reason: str) -> str:
        if not candidates_raw:
            fallback_id = "OPTIMAL_0"
        else:
            fallback_id = max(candidates_raw, key=lambda c: c.get("score", 0)).get("id", candidates_raw[0]["id"])
        logger.info(f"[LocationDecision] Fast-Path fallback ({reason}): {fallback_id}")
        return json.dumps({
            "type": "location_decision_result",
            "payload": {"agent_id": agent_id, "chosen_id": fallback_id, "reason": f"fallback:{reason}"}
        })

    try:
        payload = LocationDecisionPayload(**(payload_raw))
        logger.info(f"[LocationDecision] 수신: agent={payload.agent_id}, candidates={len(payload.candidates)}")

        if not payload.candidates:
            return _fast_path_fallback("no_candidates")

        valid_ids = {c.id for c in payload.candidates}

        # 후보 목록 텍스트 구성
        candidate_lines = "\n".join(
            f"  {c.id} | {c.category} | dist={c.dist_to_enemy:.0f} cover={c.cover_rating:.2f} height={c.height_delta:+.0f} score={c.score:.2f}"
            for c in payload.candidates
        )

        prompt = _LOCATION_DECISION_PROMPT.format(
            agent_id=payload.agent_id,
            context=payload.context_summary,
            candidate_lines=candidate_lines,
        )

        llm = get_llm(model_name="gemma4_e2b", temperature=0.0, num_predict=20)
        raw = await asyncio.to_thread(llm.invoke, prompt)
        text = (raw.content if hasattr(raw, "content") else str(raw)).strip().split()[0].upper()

        # 유효한 ID인지 검증 (대소문자 무시)
        chosen_id = next((vid for vid in valid_ids if vid.upper() == text), None)

        if not chosen_id:
            logger.warning(f"[LocationDecision] LLM 응답 '{text}'이 유효한 ID 아님 → Fast-Path")
            return _fast_path_fallback("invalid_llm_response")

        reason = f"LLM chose {chosen_id} ({payload.context_summary})"
        logger.info(f"[LocationDecision] 결과: agent={payload.agent_id} chosen={chosen_id}")

        return json.dumps({
            "type": "location_decision_result",
            "payload": {"agent_id": payload.agent_id, "chosen_id": chosen_id, "reason": reason}
        })

    except Exception as e:
        logger.error(f"[LocationDecision] 오류: {e}\n{traceback.format_exc()}")
        return _fast_path_fallback("exception")


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
