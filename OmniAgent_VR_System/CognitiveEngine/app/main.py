import os
os.environ["TORCH_DYNAMO_DISABLE"] = "1"

import json
import logging
import random
import traceback
import asyncio

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.responses import HTMLResponse, FileResponse
from pydantic import BaseModel, ValidationError
from typing import Optional
from contextlib import asynccontextmanager

from .schemas.envelope import MessageEnvelope, EEnvelopeType, PromptPayload, LocationDecisionPayload, EmergencyReportPayload
from .schemas.vr_context import GesPrompt, GestureData
from .schemas.actions import ActionBatch, ModeActionRequest, GameAction, EAction, NPCBehaviorMode, NPCFacialState
from .schemas.npc_audio import NpcAudioResponse, AudioStreamInfo, AnimationMetadata
from .clients import tts_client
from .agents.state import AgentState
from .graph import app_graph
from .utils import db_manager
from .utils import llm_factory
from .middleware import validate_auth_token, is_stale_packet, build_failed_event

# 핸들러 없는 logger 는 INFO 레벨 메시지가 콘솔에 출력되지 않는다 (Python 기본 lastResort
# 핸들러는 WARNING 이상만 처리). uvicorn 도 자기 logger 만 설정하므로 명시적으로 잡아준다.
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s | %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("api")
logger.setLevel(logging.INFO)

# SLM Reflex(긴급 반사) 발동 danger 임계. C++ CombatDangerThreshold(0.5) 와 정합.
# 이 미만(친화적/저위협 perception)은 반사 생략 → 불필요한 SLM 호출·로그 방지.
SLM_REFLEX_DANGER_THRESHOLD = float(os.environ.get("SLM_REFLEX_DANGER_THRESHOLD", "0.5"))

async def _check_ollama_model() -> None:
    try:
        import httpx, time as _t
        # 끝 슬래시 방어 — 환경변수 OLLAMA_BASE_URL 이 "http://.../" 로 끝나면
        # `{base}/api/tags` 가 `//api/tags` 가 되어 Ollama 가 307 redirect 반환.
        ollama_base = llm_factory.OLLAMA_BASE_URL.rstrip("/")
        async with httpx.AsyncClient(follow_redirects=True, timeout=30.0) as client:
            r = await client.get(f"{ollama_base}/api/tags", timeout=5.0)
            if r.status_code != 200 or not r.text.strip():
                logger.warning(f"[Startup] Ollama 응답 비정상 (status={r.status_code}) — 서버 미실행 가능")
                return
            installed = [m["name"] for m in r.json().get("models", [])]
            required = llm_factory.MODELS[llm_factory.DEFAULT_MODEL]
            if not any(required in m for m in installed):
                logger.warning(f"[Startup] 기본 모델 '{required}' Ollama에 없음 — 첫 LLM 호출 시 오류 발생 가능")
                return
            logger.info(f"[Startup] Ollama 기본 모델 확인 완료: {required}")

            # Pre-warm — 첫 location_decision 호출이 cold-start 4~6초 걸려 매번
            # stale 처리되는 문제 해소. dummy raw 호출로 모델을 메모리에 로드.
            slm_id = llm_factory.MODELS.get("gemma4_slm", "gemma4:e4b")
            _t0 = _t.perf_counter()
            warm = await client.post(f"{ollama_base}/api/generate", json={
                "model": slm_id, "prompt": "warmup", "stream": False, "raw": True,
                "keep_alive": "5m", "options": {"num_predict": 1},
            })
            _dt = (_t.perf_counter() - _t0) * 1000.0
            logger.info(f"[Startup] SLM pre-warm ({slm_id}) {_dt:.0f}ms status={warm.status_code}")
    except Exception as e:
        logger.warning(f"[Startup] Ollama 모델 상태 확인 실패 (서버 미실행 가능): {e}")


@asynccontextmanager
async def lifespan(app: FastAPI):
    # --- Startup ---
    await db_manager.init_db()
    await db_manager.start_background_sync()
    await _check_ollama_model()
    yield
    # --- Shutdown ---
    await db_manager.stop_background_sync()

app = FastAPI(lifespan=lifespan)


_cached_world_state: Optional[dict] = None
_failed_action_history: list = []
_world_state_lock = asyncio.Lock()
_action_history_lock = asyncio.Lock()
_active_llm_ws: Optional[WebSocket] = None
# fire-and-forget 태스크 강한 참조 유지 — 미보유 시 GC 가 실행 중 태스크를 수거해 무음 중단.
_background_tasks: set = set()


@app.get("/")
async def health_check():
    return {"message": "OmniAgent Cognitive Engine is running"}


@app.websocket("/ws/llm")
async def websocket_llm_endpoint(websocket: WebSocket):
    global _active_llm_ws
    await websocket.accept()
    _active_llm_ws = websocket
    logger.info("[Main] UE5 LLM 클라이언트 연결됨")

    try:
        while True:
            raw_data = await websocket.receive_text()
            response = await _process_llm_message(raw_data)
            await websocket.send_text(response)

    except WebSocketDisconnect:
        logger.info("[Main] UE5 LLM 클라이언트 연결 종료")
    finally:
        # 다중 클라이언트 레이스 방지 — 현재 끊기는 소켓이 활성 소켓일 때만 해제.
        if _active_llm_ws is websocket:
            _active_llm_ws = None

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
        # 임계값 10초: SLM (gemma e4b) thinking 호출이 cold-start 6초 + warm 2.2초.
        # 같은 이벤트루프에서 location_decision 처리 중이면 후속 패킷이 큐 대기로 자연스럽게
        # 2~5초 묵혀짐 → 임계값 2초는 정상 패킷도 드랍. (2026-05-16 직접 측정)
        if is_stale_packet(envelope.timestamp, threshold_seconds=10.0):
            logger.warning(f"[Main] Stale 패킷 드랍. msg_id={envelope.msg_id}")
            return json.dumps({"status": "dropped", "reason": "stale_packet", "msg_id": envelope.msg_id})

        # ── Step 4: 타입별 분기 ───────────────────────────────────────
        if envelope.type == EEnvelopeType.PROMPT:
            return await _handle_prompt(envelope)

        elif envelope.type == EEnvelopeType.STATE_UPDATE:
            return await _handle_state_update(envelope)

        elif envelope.type == EEnvelopeType.ACTION_FAILED:
            return await _handle_action_failed(envelope)
            
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

# NOTE: 이전에 존재했던 @app.websocket("/ws/slm") 엔드포인트는 제거됨.
# 모든 emergency_report는 /ws/llm으로 들어오고, _handle_emergency_report 내부에서
# _handle_slm_reflex로 자동 라우팅됨 → 단일 채널로 통합.

_REFLEX_FACIAL: dict = {
    "Attack": "Angry", "Block": "Fear", "Dodge": "Surprised",
    "Flee": "Fear", "SignalAllies": "Surprised", "Scan": "Surprised",
}

_REFLEX_PROMPT = """\
NPC '{agent_id}' detects a threat:
- Target: {target_id} (Affinity: {affinity_score} [{affinity_tag}])
- Sense: {sense}, Distance: {dist:.1f}m, Danger: {danger:.2f}
{extra_lines}

Action selection guidance:
- Hostile target (affinity <= -30): Attack if close, Flee if low HP, Block/Dodge under attack
- Neutral target (-29 ~ 29): Scan to assess, SignalAllies for backup
- Friendly target: Scan only

Choose ONE immediate action: Attack, Block, Dodge, Flee, SignalAllies, Scan
Reply with ONLY the action name, e.g.: Attack"""


def _empty_batch_json(mode: NPCBehaviorMode = "Common") -> str:
    """액션 없는 기본 ModeActionRequest JSON — 폴백/무행동 공통 응답."""
    return ModeActionRequest(Mode=mode, ActionBatches={}).model_dump_json()


async def _handle_slm_reflex(payload: EmergencyReportPayload) -> str:
    """
    SLM 반사 행동 결정 (목표 500ms).
    LangGraph 없이 단일 경량 SLM 호출로 즉각 전투/회피 액션 생성.
    """
    from .utils.llm_factory import get_llm
    from .schemas.actions import ActionBatch, GameAction, ModeActionRequest
    from .utils import db_manager

    agent_id = payload.agent_id
    perceptions = payload.perceptions

    if not perceptions:
        batch = ActionBatch(AgentID=agent_id, Mode="Combat", Actions=[
            GameAction(ActionType="Scan", FacialState="Surprised", Parameters={})
        ])
        return ModeActionRequest(Mode="Combat", ActionBatches={agent_id: batch}).model_dump_json()

    # 플레이어 적대 행동에 따른 호감도 감소.
    # danger_score >= 0.5 인 perception(공격/심한 위협)을 일으킨 대상에게 -5씩 감점.
    for p in perceptions:
        if p.danger_score >= 0.5 and p.target_id:
            # 캐시 prime — 미존재 시 DB에서 로드하거나 기본값(0)으로 생성
            await db_manager.get_affinity(agent_id, p.target_id)
            db_manager.update_affinity_sync(
                source_id=agent_id,
                target_id=p.target_id,
                score_delta=-5,
                interaction_summary=f"Hostile {p.sense_type} (danger={p.danger_score:.2f})"
            )
            logger.info(f"[Affinity] {agent_id} → {p.target_id}: -5 (적대 perception)")

    top = max(perceptions, key=lambda p: p.danger_score)

    # 가장 위협적인 대상의 현재 호감도 조회 (SLM 프롬프트와 폴백 로직에 사용)
    top_relation = await db_manager.get_affinity(agent_id, top.target_id) if top.target_id else None
    top_score = top_relation.affinity_score if top_relation else 0
    top_tag = top_relation.reputation_tag if top_relation else "Neutral"

    extra_lines = ""
    if len(perceptions) > 1:
        others = [f"  - {p.target_id} ({p.sense_type}, dist {p.distance:.1f})" for p in perceptions[1:3]]
        extra_lines = "Other threats:\n" + "\n".join(others)

    prompt = _REFLEX_PROMPT.format(
        agent_id=agent_id,
        target_id=top.target_id,
        affinity_score=top_score,
        affinity_tag=top_tag,
        sense=top.sense_type,
        dist=top.distance,
        danger=top.danger_score,
        extra_lines=extra_lines,
    )

    # SLM 결과를 신뢰. 호출 자체가 실패한 예외 상황에만 안전한 기본 액션(Scan)으로 폴백.
    action_type = "Scan"
    try:
        llm = get_llm(model_name="gemma4_slm", temperature=0.3, num_predict=10)
        raw = await asyncio.to_thread(llm.invoke, prompt)
        raw_text = (raw.content if hasattr(raw, "content") else str(raw)).strip()
        tokens = raw_text.split()
        text = tokens[0] if tokens else ""

        valid = {"Attack", "Block", "Dodge", "Flee", "SignalAllies", "Scan"}
        if text.capitalize() in valid:
            action_type = text.capitalize()
        elif text:
            # 키워드 검색 폴백 (SLM이 잡담을 끼워넣은 경우)
            text_l = raw_text.lower()
            for a in valid:
                if a.lower() in text_l:
                    action_type = a
                    break

    except Exception as e:
        logger.error(f"[SLM] 추론 실패, 안전 폴백(Scan) 사용: {e}")
        # action_type은 이미 "Scan"으로 초기화됨

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

        # ── danger 게이트 ──────────────────────────────────────────────
        # SLM Reflex 는 "긴급 전투(0.5초 반사)" 용. 친화적/저위협(예: 호감도 높은
        # 플레이어를 시야에 둠, danger<0.5) perception 까지 매번 SLM 을 때리면
        # 낭비·로그도배. 최고 danger 가 임계 미만이면 반사 생략(무행동).
        max_danger = max((p.danger_score for p in payload.perceptions), default=0.0)
        if max_danger < SLM_REFLEX_DANGER_THRESHOLD:
            logger.info(
                f"[Main] 비긴급(maxdanger={max_danger:.2f}<{SLM_REFLEX_DANGER_THRESHOLD}) "
                f"— SLM Reflex 생략. npc={payload.agent_id}"
            )
            return _empty_batch_json()

        # ── [핵심 최적화] 긴급 전투 상황의 0.5초 반사 신경(Reflex) 라우팅 ──
        logger.info(f"[Main] LangGraph 우회: {payload.agent_id}의 긴급 상황을 SLM Reflex로 즉시 처리합니다.")
        return await _handle_slm_reflex(payload)

    except Exception as e:
        logger.error(f"[Main] _handle_emergency_report 실행 중 치명적 오류: {e}")
        import traceback
        traceback.print_exc()
        return _empty_batch_json()


def _trigger_dialogue_audio(final_action: Optional[ActionBatch],
                            fallback_npc: Optional[str], trace_id: str) -> None:
    """ActionBatch 의 Dialogue 액션을 찾아 TTS dispatch 백그라운드 태스크 생성.

    Dialogue 없거나 대상 없으면 생략. 글자 없는 대사("...")는 bypass_tts(자막만 전송).
    """
    npc_id_for_audio = final_action.AgentID if final_action else fallback_npc
    dialogue_text: Optional[str] = None
    dialogue_emotion: str = "Neutral"   # M3: Dialogue FacialState → TTS emotion
    if final_action and final_action.Actions:
        for act in final_action.Actions:
            if act.ActionType == "Dialogue":
                # NPCActionKeys::Key_Text == "text"
                dialogue_text = act.Parameters.get("text") or None
                dialogue_emotion = act.FacialState or "Neutral"
                if dialogue_text:
                    break

    if not npc_id_for_audio:
        logger.info("[Main][TTS] target_npc 미지정 → 발화 대상 없음, dispatch 생략")
        return
    if not dialogue_text:
        logger.info(f"[Main][TTS] {npc_id_for_audio} ActionBatch 에 Dialogue 없음 → dispatch 생략")
        return

    # 글자 없는 대사("...")는 TTS 만 생략(bypass_tts)하되 자막은 전송(빈 url) — isalnum 은 한글 포함.
    has_speech = any(c.isalnum() for c in dialogue_text)
    task = asyncio.create_task(_dispatch_npc_audio(
        npc_id=npc_id_for_audio,
        dialogue_text=dialogue_text,
        emotion=dialogue_emotion,
        trace_id=trace_id,
        bypass_tts=not has_speech,
    ))
    _background_tasks.add(task)
    task.add_done_callback(_background_tasks.discard)


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

    target_npc_from_payload = prompt_payload.target_npc_id or None

    async with _world_state_lock:
        world_snap = _cached_world_state
    async with _action_history_lock:
        history_snap = list(_failed_action_history)
        _failed_action_history.clear()

    initial_state: AgentState = AgentState(
        messages=[],
        vr_context=ges_prompt,
        cached_world_state=world_snap,
        failed_action_history=history_snap,
        next="",
        current_speaker="",
        natural_context=None,
        raw_response=None,
        target_npc=target_npc_from_payload,
        behavior_mode=None,
        facial_state=None,
        action_batch=None,
        target_npcs=[target_npc_from_payload] if target_npc_from_payload else [],
        msg_id=envelope.msg_id,
        timestamp=envelope.timestamp,
        has_error=False,
        error_msg=None,
    )

    logger.info("[Main] Graph 비동기 실행 시작...")
    try:
        result = await app_graph.ainvoke(initial_state)
    except Exception as e:
        logger.error(f"[Main] LangGraph 실행 중 치명적 오류: {e}")
        traceback.print_exc()
        return _empty_batch_json()

    final_action: Optional[ActionBatch] = result.get("action_batch")

    # ── TTS 트리거 (M2) ──────────────────────────────────────────────────
    # LLM 이 ActionBatch 에 Dialogue 액션을 넣으면 그 text 를 합성 요청(없으면 발화 안 함).
    _trigger_dialogue_audio(final_action, target_npc_from_payload, envelope.msg_id)

    if final_action:
        logger.info(f"[Main] ActionBatch 생성 완료: agent_id={final_action.AgentID}")
        wrapper = ModeActionRequest(
            Mode=final_action.Mode,
            ActionBatches={final_action.AgentID: final_action}
        )
        return wrapper.model_dump_json()
    else:
        logger.warning("[Main] 에이전트가 ActionBatch를 생성하지 않았습니다.")
        return _empty_batch_json()


async def _dispatch_npc_audio(npc_id: str, dialogue_text: str, emotion: str,
                              trace_id: str = "", bypass_tts: bool = False) -> None:
    """TTS 합성 요청 후 활성 UE5 WS 로 NpcAudioResponse 푸시.

    trace_id: 발원 envelope.msg_id — TTS request_id 로 상속되어 로그 체인 통일.
    bypass_tts: 글자 없는 대사("...") — TTS 합성 생략, 자막만 전송(빈 url).
    실패 시 자막만 담은 응답(audio_stream.url 빈 문자열) 전송 — UE5 측 fallback.
    """
    # M2: voice_id 자리에 npc_id 를 그대로 전달.
    # TTSService 가 voice_map.yaml 을 참조해 실제 모델 voice 로 변환.
    if bypass_tts:
        logger.info(f"[Main][TTS][trace={trace_id}] 글자 없는 대사 → TTS 생략, 자막만 전송. npc={npc_id}")
        info = {"request_id": "", "ws_url": "", "sample_rate": 16000, "channels": 1}
    else:
        try:
            info = await tts_client.synthesize(
                text=dialogue_text,
                voice_id=npc_id,
                emotion=emotion,
                trace_id=trace_id,
            )
        except tts_client.TTSError as e:
            logger.warning(f"[Main][TTS][trace={trace_id}] 합성 실패 → 자막만 전송. npc={npc_id}, err={e}")
            info = {"request_id": "", "ws_url": "", "sample_rate": 16000, "channels": 1}

    if _active_llm_ws is None:
        logger.info("[Main][TTS] 활성 UE5 WS 없음 — NpcAudioResponse 송신 생략")
        return

    response = NpcAudioResponse(
        request_id=info["request_id"],
        npc_id=npc_id,
        dialogue_text=dialogue_text,
        audio_stream=AudioStreamInfo(
            url=info["ws_url"],
            sample_rate=info.get("sample_rate", 16000),
            channels=info.get("channels", 1),
        ),
        animation_metadata=AnimationMetadata(emotion=emotion),
    )
    try:
        await _active_llm_ws.send_text(response.model_dump_json())
        logger.info(f"[Main][TTS][trace={trace_id}] NpcAudioResponse 전송. npc={npc_id}, req={info['request_id']}")
    except Exception as e:
        logger.warning(f"[Main][TTS] NpcAudioResponse 전송 실패: {e}")



async def _handle_state_update(envelope: MessageEnvelope) -> str:
    global _cached_world_state

    try:
        state_payload = envelope.parse_state_update_payload()
        async with _world_state_lock:
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
Task: choose one tactical position name.

Example 1:
Situation: HP:30% Enemies:3
Candidates: SAFE, OPTIMAL, AGGRESSIVE
Answer: SAFE

Example 2:
Situation: HP:90% Enemies:1
Candidates: SAFE, OPTIMAL, AGGRESSIVE
Answer: AGGRESSIVE

Example 3:
Situation: {context}
Candidates: {candidate_ids}
Answer:"""


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
    # UE5 가 보낸 EQS 요청 세대 번호 — 응답에 그대로 echo. UE5 는 stale 응답 차단에 사용.
    request_gen = int(payload_raw.get("request_gen", 0))

    def _fast_path_fallback(reason: str) -> str:
        if not candidates_raw:
            fallback_id = "OPTIMAL_0"
            reason_str = "no_candidates"
        else:
            import random
            sorted_candidates = sorted(candidates_raw, key=lambda c: c.get("score", 0), reverse=True)
            top_n = sorted_candidates[:3]
            roll = random.randint(1, 100)

            if roll > 40:  # 60% chance to act rationally
                fallback_id = top_n[0].get("id", "OPTIMAL_0")
                reason_str = f"Fast-Path (Roll: {roll}): Calmly chose optimal cover"
            else:          # 40% chance to panic
                fallback_id = random.choice(top_n[1:] if len(top_n) > 1 else top_n).get("id", "OPTIMAL_0")
                reason_str = f"Fast-Path (Roll: {roll}): Panicked! Chose suboptimal cover"

        logger.info(f"[LocationDecision] {reason_str}: {fallback_id} (fallback reason: {reason})")
        return json.dumps({
            "type": "location_decision_result",
            "payload": {"agent_id": agent_id, "chosen_id": fallback_id, "reason": reason_str, "request_gen": request_gen}
        })

    try:
        payload = LocationDecisionPayload(**(payload_raw))
        logger.info(f"[LocationDecision] 수신: agent={payload.agent_id}, candidates={len(payload.candidates)}")

        if not payload.candidates:
            return _fast_path_fallback("no_candidates")

        valid_ids = {c.id for c in payload.candidates}

        # Few-shot raw 프롬프트 — gemma e4b thinking 우회.
        # 직접 측정 (2026-05-16): chat template thinking ~2200ms/289토큰 → raw few-shot ~400ms/4토큰.
        candidate_ids = ", ".join(c.id for c in payload.candidates)
        prompt = _LOCATION_DECISION_PROMPT.format(
            context=payload.context_summary,
            candidate_ids=candidate_ids,
        )

        # Ollama 직접 호출 — raw=true 로 chat template (thinking 동반) 우회.
        # langchain ChatOllama 는 raw 옵션 지원이 약해 httpx 로 직접 호출.
        import httpx, time as _t
        ollama_base = llm_factory.OLLAMA_BASE_URL.rstrip("/")
        model_id = llm_factory.MODELS.get("gemma4_slm", "gemma4:e4b")
        body = {
            "model": model_id,
            "prompt": prompt,
            "stream": False,
            "raw": True,
            "keep_alive": "5m",
            "options": {"temperature": 0.0, "num_predict": 10, "stop": ["\n"]},
        }
        _llm_start = _t.perf_counter()
        async with httpx.AsyncClient(timeout=20.0) as client:
            resp = await client.post(f"{ollama_base}/api/generate", json=body)
        _llm_ms = (_t.perf_counter() - _llm_start) * 1000.0
        raw_text = (resp.json().get("response") or "").strip()
        logger.info(f"[LocationDecision] LLM {_llm_ms:.0f}ms raw={raw_text!r} (gen={request_gen})")

        tokens = raw_text.split()
        if not tokens:
            logger.warning(f"[LocationDecision] LLM 빈 응답 agent={payload.agent_id} → Fast-Path")
            return _fast_path_fallback("empty_llm_response")
        text = tokens[0].upper()

        # 유효한 ID인지 검증 (대소문자 무시)
        chosen_id = next((vid for vid in valid_ids if vid.upper() == text), None)

        if not chosen_id:
            logger.warning(f"[LocationDecision] LLM 응답 '{text}'이 유효한 ID 아님 → Fast-Path")
            return _fast_path_fallback("invalid_llm_response")

        reason = f"LLM chose {chosen_id} ({payload.context_summary})"
        logger.info(f"[LocationDecision] 결과: agent={payload.agent_id} chosen={chosen_id}")

        return json.dumps({
            "type": "location_decision_result",
            "payload": {"agent_id": payload.agent_id, "chosen_id": chosen_id, "reason": reason, "request_gen": request_gen}
        })

    except Exception as e:
        logger.error(f"[LocationDecision] 오류: {e}\n{traceback.format_exc()}")
        return _fast_path_fallback("exception")


# ─────────────────────────────────────────────────────────────────────────────
# Debug Dashboard
# ─────────────────────────────────────────────────────────────────────────────

import os
import yaml

_DEBUG_HTML_PATH = os.path.join(os.path.dirname(__file__), "debug.html")
_PERSONAS_BASE = os.path.join(os.path.dirname(__file__), "agents", "personas")


def _list_personas() -> list[dict]:
    """등록된 모든 NPC 페르소나 YAML을 스캔하여 반환."""
    results = []
    for subdir in ("core", "generic"):
        folder = os.path.join(_PERSONAS_BASE, subdir)
        if not os.path.isdir(folder):
            continue
        for fname in sorted(os.listdir(folder)):
            if not fname.endswith(".yaml"):
                continue
            fpath = os.path.join(folder, fname)
            try:
                with open(fpath, "r", encoding="utf-8") as f:
                    data = yaml.safe_load(f) or {}
                results.append({
                    "npc_id": data.get("name", fname[:-5]),
                    "file_path": fpath,
                    "folder": subdir,
                    "role": data.get("role", ""),
                    "importance": data.get("importance", "normal"),
                    "traits": data.get("traits", []),
                })
            except Exception:
                pass
    return results


@app.get("/debug", response_class=HTMLResponse)
async def debug_dashboard():
    if os.path.exists(_DEBUG_HTML_PATH):
        with open(_DEBUG_HTML_PATH, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read())
    return HTMLResponse(content="<h1>debug.html not found</h1>", status_code=404)


@app.get("/api/affinity")
async def api_get_affinity():
    rows = await db_manager.get_all_affinity()
    return {"rows": rows}


class AffinitySetRequest(BaseModel):
    source_id: str
    target_id: str
    score: int
    note: str = "debug_override"

@app.post("/api/affinity")
async def api_set_affinity(req: AffinitySetRequest):
    rel = await db_manager.set_affinity_direct(req.source_id, req.target_id, req.score, req.note)
    return {"status": "ok", "source_id": rel.source_id, "target_id": rel.target_id,
            "affinity_score": rel.affinity_score, "reputation_tag": rel.reputation_tag}


@app.delete("/api/affinity")
async def api_delete_affinity(source_id: str, target_id: str):
    await db_manager.delete_affinity(source_id, target_id)
    return {"status": "deleted"}


@app.get("/api/npcs")
async def api_get_npcs():
    return {"npcs": _list_personas()}


class ImportanceUpdateRequest(BaseModel):
    importance: str  # "normal" | "high" | "core"

@app.put("/api/npcs/{npc_id}/importance")
async def api_set_importance(npc_id: str, req: ImportanceUpdateRequest):
    if req.importance not in ("normal", "high", "core"):
        raise HTTPException(status_code=400, detail="importance must be normal, high, or core")

    personas = _list_personas()
    target = next((p for p in personas if p["npc_id"].lower() == npc_id.lower()), None)
    if not target:
        raise HTTPException(status_code=404, detail=f"NPC '{npc_id}' persona not found")

    fpath = target["file_path"]
    try:
        with open(fpath, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f) or {}
        data["importance"] = req.importance
        with open(fpath, "w", encoding="utf-8") as f:
            yaml.dump(data, f, allow_unicode=True, default_flow_style=False)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

    if req.importance == "core":
        llm_model = "gemma4:26b"
    elif req.importance == "high":
        llm_model = "qwen3:8b"
    else:
        llm_model = "gemma4:e4b"
    return {"status": "ok", "npc_id": npc_id, "importance": req.importance, "llm_model": llm_model}


class NpcCommandRequest(BaseModel):
    action_type: str
    mode: str = "Common"
    facial: str = "Neutral"
    params: dict = {}

@app.post("/api/npc/{npc_id}/command")
async def api_npc_command(npc_id: str, req: NpcCommandRequest):
    if not _active_llm_ws:
        raise HTTPException(status_code=503, detail="UE5 연결 없음 (WebSocket disconnected)")
    try:
        batch = ActionBatch(
            AgentID=npc_id,
            Mode=req.mode,
            Actions=[GameAction(
                ActionType=req.action_type,
                FacialState=req.facial,
                Parameters={k: str(v) for k, v in req.params.items()}
            )]
        )
        wrapper = ModeActionRequest(Mode=req.mode, ActionBatches={npc_id: batch})
        await _active_llm_ws.send_text(wrapper.model_dump_json())
        logger.info(f"[Debug] NPC 명령 전송: {npc_id} → {req.action_type}")
        return {"status": "ok", "npc_id": npc_id, "action": req.action_type}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@app.get("/api/ws/status")
async def api_ws_status():
    return {"connected": _active_llm_ws is not None}


class DebugPromptRequest(BaseModel):
    npc_id: str
    text: str
    player_id: str = "Debug_Player"


@app.post("/api/debug/prompt")
async def api_debug_prompt(req: DebugPromptRequest):
    """디버그: UE 없이 콘솔/웹에서 NPC 에게 직접 말 걸기.
    PROMPT envelope 를 만들어 그래프 실행 → ActionBatch(JSON) 반환.
    TTS dispatch 도 _handle_prompt 내부에서 함께 동작(활성 UE WS 있으면 음성 푸시)."""
    import uuid
    import time as _t
    env = MessageEnvelope(
        msg_id=str(uuid.uuid4()),
        # auth_token 은 WS 수신 루프에서만 검증됨. 디버그는 _handle_prompt 직접 호출이라
        # 검증을 거치지 않지만 pydantic 필수 필드라 env 값(없으면 더미)으로 채운다.
        auth_token=os.environ.get("WS_AUTH_TOKEN", "debug"),
        timestamp=_t.time(),
        type=EEnvelopeType.PROMPT,
        payload={
            "player_id": req.player_id,
            "voice_transcript": req.text,
            "target_npc_id": req.npc_id,
        },
    )
    try:
        result_json = await _handle_prompt(env)
        return json.loads(result_json)
    except Exception as e:  # noqa: BLE001
        logger.exception(f"[Debug] prompt 처리 실패: {e}")
        raise HTTPException(status_code=500, detail=str(e))


async def _handle_action_failed(envelope: MessageEnvelope) -> str:
    global _failed_action_history

    failed_event = build_failed_event(envelope)
    async with _action_history_lock:
        _failed_action_history.append(failed_event)

    logger.warning(
        f"[Main] 명령 실패 이력 기록. ref_msg_id={envelope.ref_msg_id}, "
        f"action={failed_event.get('failed_action_type')}, "
        f"reason={failed_event.get('reason')}"
    )

    return json.dumps({"status": "logged", "msg_id": envelope.msg_id})
