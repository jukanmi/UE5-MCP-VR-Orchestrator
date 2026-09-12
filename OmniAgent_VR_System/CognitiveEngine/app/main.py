import os

os.environ["TORCH_DYNAMO_DISABLE"] = "1"

import json
import logging
import traceback
import asyncio
import yaml

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, ValidationError
from typing import Dict, Optional
from contextlib import asynccontextmanager

from .schemas.envelope import (
    MessageEnvelope,
    EEnvelopeType,
    PromptPayload,
    LocationDecisionPayload,
    EmergencyReportPayload,
)
from .schemas.vr_context import GesPrompt
from .schemas.actions import (
    ActionBatch,
    ModeActionRequest,
    GameAction,
    NPCBehaviorMode,
)
from .agents.state import AgentState
from .graph import app_graph
from .utils import db_manager
from .utils import llm_factory
from .utils.async_tasks import spawn_background
from .middleware import validate_auth_token, is_stale_packet

# 핸들러 없는 logger 는 INFO 레벨 메시지가 콘솔에 출력되지 않는다 (Python 기본 lastResort
# 핸들러는 WARNING 이상만 처리). uvicorn 도 자기 logger 만 설정하므로 명시적으로 잡아준다.
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s | %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("api")
logger.setLevel(logging.INFO)

# emergency 통보 게이트 danger 임계. C++ CombatDangerThreshold(0.5) 와 정합.
# 이 미만(친화적/저위협 perception)은 호감도 감점 대상도 아니라 통보만 하고 끝낸다.
#
# 이름이 SLM 인 것은 잔재다 — SLM 반사는 2026-08-19 제거됐고 이 상수는 통보 게이트로 유임됐다.
# 리네임하지 말 것: env var 키로도 읽히므로 이름을 바꾸면 기존 배포·실행 스크립트의
# 오버라이드가 **조용히** 무시된다(에러 없이 기본값 0.5 로 돌아감).
SLM_REFLEX_DANGER_THRESHOLD = float(os.environ.get("SLM_REFLEX_DANGER_THRESHOLD", "0.5"))


async def _check_ollama_model() -> None:
    try:
        import httpx
        import time as _t

        # 끝 슬래시 방어 — 환경변수 OLLAMA_BASE_URL 이 "http://.../" 로 끝나면
        # `{base}/api/tags` 가 `//api/tags` 가 되어 Ollama 가 307 redirect 반환.
        ollama_base = llm_factory.OLLAMA_BASE_URL.rstrip("/")
        async with httpx.AsyncClient(follow_redirects=True, timeout=30.0) as client:
            r = await client.get(f"{ollama_base}/api/tags", timeout=5.0)
            if r.status_code != 200 or not r.text.strip():
                logger.warning(f"[Startup] Ollama 응답 비정상 (status={r.status_code}) — 서버 미실행 가능")
                return
            installed = [m["name"] for m in r.json().get("models", [])]
            required = llm_factory.MODELS[llm_factory.STAGE2_MODEL]
            if not any(required in m for m in installed):
                logger.warning(f"[Startup] 기본 모델 '{required}' Ollama에 없음 — 첫 LLM 호출 시 오류 발생 가능")
                return
            logger.info(f"[Startup] Ollama 기본 모델 확인 완료: {required}")

            # Pre-warm — 첫 location_decision 호출이 cold-start 4~6초 걸려 매번
            # stale 처리되는 문제 해소. dummy raw 호출로 모델을 메모리에 로드.
            slm_id = llm_factory.MODELS.get("gemma4_slm", "gemma4:e4b")
            _t0 = _t.perf_counter()
            warm = await client.post(
                f"{ollama_base}/api/generate",
                json={
                    "model": slm_id,
                    "prompt": "warmup",
                    "stream": False,
                    "raw": True,
                    "keep_alive": "5m",
                    "options": {"num_predict": 1},
                },
            )
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


# state_update 는 NPC 마다 자기 owner_agent_id 기준 payload 를 보낸다. 전역 1개로 두면
# 마지막에 보고한 NPC 의 perception 이 다른 NPC 의 프롬프트에 주입돼 지식 격리가 깨진다.
# owner_agent_id(소문자) → payload 로 분리 보관하고, 프롬프트 조립 시 대상 NPC 것만 꺼낸다.
_cached_world_states: Dict[str, dict] = {}
_world_state_lock = asyncio.Lock()
_active_llm_ws: Optional[WebSocket] = None
# WS 송신 직렬화 — 메시지별 동시 처리가 같은 소켓에 겹쳐 쓰는 것 방지.
_ws_send_lock = asyncio.Lock()
_last_core_prewarm: float = 0.0
_CORE_PREWARM_THROTTLE_S = 30.0  # keep_alive(30s) 와 동일 — 윈도 내 중복 웜업 무의미


async def _ollama_raw_generate(prompt: str) -> tuple[str, float]:
    """gemma e4b raw=true 단발 생성 — chat template(thinking) 우회. (raw_text, elapsed_ms) 반환.
    SLM Reflex·location_decision 공용. 커넥션 풀 재사용 + stop=["\n"] 안전 마진.
    raise_for_status 로 4xx/5xx 는 예외 → 호출부 except 가 안전 폴백 처리."""
    import time as _t

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
    resp = await llm_factory.get_ollama_client().post(f"{ollama_base}/api/generate", json=body)
    resp.raise_for_status()
    elapsed_ms = (_t.perf_counter() - _llm_start) * 1000.0
    raw_text = (resp.json().get("response") or "").strip()
    return raw_text, elapsed_ms


async def _prewarm_core_llm() -> None:
    """Stage2 플래너를 빈 프롬프트 로드콜로 메모리에 올린다. 실패는 무해(콜드 폴백).
    throttle: keep_alive(30s) 와 동일 — 윈도 내 중복 웜업은 의미 없다."""
    global _last_core_prewarm
    import time as _t

    now = _t.monotonic()
    if now - _last_core_prewarm < _CORE_PREWARM_THROTTLE_S:
        return
    _last_core_prewarm = now
    try:
        ollama_base = llm_factory.OLLAMA_BASE_URL.rstrip("/")
        model_id = llm_factory.MODELS[llm_factory.STAGE2_MODEL]
        # prompt 키 없음 = Ollama 로드콜 전용 (342ms) — num_predict=1 생성(3.7s) 아님.
        # keep_alive 는 llm_factory core 분기 값과 반드시 일치시킬 것 — 다르면 squat 정책 오버라이드.
        # raise_for_status 하지 말 것 — 4xx/5xx 도 무해 폴백.
        await llm_factory.get_ollama_client().post(
            f"{ollama_base}/api/generate",
            json={"model": model_id, "keep_alive": "30s"},
        )
        logger.info("[Prewarm] Stage2 플래너 로드콜 완료")
    except Exception as exc:
        logger.warning(f"[Prewarm] Stage2 웜업 실패(무해 폴백): {exc}")


@app.get("/")
async def health_check():
    return {"message": "OmniAgent Cognitive Engine is running"}


@app.websocket("/ws/llm")
async def websocket_llm_endpoint(websocket: WebSocket):
    global _active_llm_ws
    await websocket.accept()
    _active_llm_ws = websocket
    logger.info("[Main] UE5 LLM 클라이언트 연결됨")

    async def _process_and_send(raw_data: str) -> None:
        # WHY 메시지별 태스크: 수신 루프에서 직렬 await 하면 대화 처리(수 초) 동안
        # 후속 location_decision/emergency 가 큐에 묵혀 stale 드랍됨 (이전 임계 10초
        # 상향이 이 증상의 우회책이었음). 응답 순서는 보장하지 않음 — prompt 는
        # msg_id, location_decision 은 request_gen 으로 수신 측이 매칭하므로 무관.
        response = await _process_llm_message(raw_data)
        try:
            async with _ws_send_lock:
                await websocket.send_text(response)
        except Exception as e:
            logger.warning(f"[Main] WS 응답 전송 실패(연결 종료 추정): {e}")

    # 이 연결이 띄운 처리 태스크 — 끊기면 취소해 버려질 응답의 LLM 추론(GPU/VRAM)을 끊는다.
    session_tasks: set[asyncio.Task] = set()

    try:
        while True:
            raw_data = await websocket.receive_text()
            task = spawn_background(_process_and_send(raw_data), label="ws-process")
            session_tasks.add(task)
            task.add_done_callback(session_tasks.discard)

    except WebSocketDisconnect:
        logger.info("[Main] UE5 LLM 클라이언트 연결 종료")
    finally:
        pending = [t for t in session_tasks if not t.done()]
        for t in pending:
            t.cancel()
        if pending:
            logger.info(f"[Main] 진행 중 처리 태스크 {len(pending)}건 취소(연결 종료)")

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
            # location_decision 은 generic drop 으로 응답하면 UE5 라우팅에 잡히지 않아
            # WaitingLLM 이 TacticalLLMTimeout 까지 유지되고 Event Report 게이트도
            # 함께 막힘 → 드랍 대신 Fast-Path 폴백 결과를 돌려줘 즉시 해제.
            if envelope.type == EEnvelopeType.LOCATION_DECISION:
                logger.warning(f"[Main] Stale location_decision → Fast-Path 폴백. msg_id={envelope.msg_id}")
                payload_raw = envelope.payload if isinstance(envelope.payload, dict) else {}
                return _location_decision_fast_path(payload_raw, "stale_packet")

            logger.warning(f"[Main] Stale 패킷 드랍. msg_id={envelope.msg_id}")
            return json.dumps(
                {
                    "status": "dropped",
                    "reason": "stale_packet",
                    "msg_id": envelope.msg_id,
                }
            )

        # ── Step 4: 타입별 분기 ───────────────────────────────────────
        if envelope.type == EEnvelopeType.PROMPT:
            return await _handle_prompt(envelope)

        elif envelope.type == EEnvelopeType.STATE_UPDATE:
            return await _handle_state_update(envelope)

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

    except json.JSONDecodeError:
        logger.error(f"[Main] JSON 파싱 실패: \n{traceback.format_exc()}")
        return json.dumps({"error": "Invalid JSON format"})

    except Exception as e:
        logger.error(f"[Main] 예기치 않은 오류: \n{traceback.format_exc()}")
        return json.dumps({"error": "Internal server error", "detail": str(e)})


def _empty_batch_json(mode: NPCBehaviorMode = "Common") -> str:
    """액션 없는 기본 ModeActionRequest JSON — 폴백/무행동 공통 응답."""
    return ModeActionRequest(Mode=mode, ActionBatches={}).model_dump_json()


async def _apply_hostile_affinity(agent_id: str, perceptions: list) -> None:
    """적대 perception(danger>=0.5)을 일으킨 대상에게 호감도 -5 감점 (side-effect)."""
    for p in perceptions:
        if p.danger_score >= 0.5 and p.target_id:
            # 캐시 prime — 미존재 시 DB에서 로드하거나 기본값(0)으로 생성
            await db_manager.get_affinity(agent_id, p.target_id)
            # 동기 sqlite 쓰기 → 이벤트 루프 블로킹 방지 위해 스레드 오프로드 (codebase idiom).
            await asyncio.to_thread(
                db_manager.update_affinity_sync,
                source_id=agent_id,
                target_id=p.target_id,
                score_delta=-5,
                interaction_summary=f"Hostile {p.sense_type} (danger={p.danger_score:.2f})",
            )
            logger.info(f"[Affinity] {agent_id} → {p.target_id}: -5 (적대 perception)")


def _record_reflex_memory(agent_id: str, reflex_action: str) -> None:
    """C++ 척수반사가 실행한 액션을 NPC 기억에 Event 로 남긴다.

    LLM 은 반사가 일어난 걸 모른다. 기록해두지 않으면 다음 replan 이 '이제 공격을 시작하라'
    같은 한 박자 늦은 지시를 만든다. 기억에 남겨두면 plan 이 '전투 돌입'이 아니라
    '전투 지속·전술' 수준에서 시작한다. 전투 승리 보고와 같은 패턴.
    """
    from .utils.memory_manager import get_memory

    def _write() -> None:
        try:
            get_memory(agent_id).add_entry("Event", f"{agent_id}이(가) 반사적으로 {reflex_action}을(를) 실행했다.")
        except Exception as e:
            # to_thread 태스크 내부 예외는 어디서도 await 안 하면 무음 소실 — 로그로 드러낸다.
            logger.error(f"[Main] 반사 이력 메모리 기록 실패: {e}")

    spawn_background(asyncio.to_thread(_write), label="reflex-memory")
    logger.info(f"[Main] 반사 이력 기록: npc={agent_id}, action={reflex_action}")


async def _handle_combat_victory(payload: EmergencyReportPayload) -> str:
    """전투 승리 보고(report_type="combat_victory") 처리 — SPEC_combat_selector Phase 2.

    행동 생성 없음(무행동 배치 반환) — 후속 행동은 UE5 replan 플래그가 강제하는
    다음 prompt 의 LLM 몫. 여기서는 승리 사실을 NPC 장기 기억에 남겨
    다음 대화에서 '내가 그놈을 처치했다'를 인지하게만 한다.
    """
    from .utils.memory_manager import get_memory

    agent_id = payload.agent_id
    defeated = payload.perceptions[0].target_id if payload.perceptions else "Unknown"
    logger.info(f"[Main] 전투 승리 보고: npc={agent_id}, defeated={defeated} — 메모리 기록, 무행동")

    # add_entry 는 파일 I/O + 토큰 예산 초과 시 요약까지 수행 가능 — 루프 블로킹 방지 오프로드.
    def _write_victory_memory() -> None:
        try:
            get_memory(agent_id).add_entry("Event", f"{agent_id}이(가) 전투에서 {defeated}을(를) 쓰러뜨렸다 (승리).")
        except Exception as e:
            # to_thread 태스크 내부 예외는 어디서도 await 안 하면 무음 소실 — 로그로 드러낸다.
            logger.error(f"[Main] 전투 승리 메모리 기록 실패: {e}")

    spawn_background(asyncio.to_thread(_write_victory_memory), label="victory-memory")

    return _empty_batch_json()


async def _handle_emergency_report(envelope: MessageEnvelope) -> str:
    try:
        payload = envelope.parse_emergency_report_payload()

        # ── 특수 보고 분기 ─────────────────────────────────────────────
        # combat_victory 는 위협이 아니라 종결 통보 — danger 게이트 전에 라우팅
        # (danger=0 이라 아래 게이트에 걸려 조용히 버려지는 것을 방지).
        if payload.report_type == "combat_victory":
            return await _handle_combat_victory(payload)

        logger.info(f"[Main] 긴급 보고 수신. npc={payload.agent_id}, perceptions={len(payload.perceptions)}")

        # ── 반사 이력 기록 ────────────────────────────────────────────
        # 반사는 UE5 가 이미 실행했다. 여기서 할 일은 그 사실을 기억에 남겨,
        # 다음 replan 이 "아직 아무것도 안 했다" 전제로 중복 지시를 내리는 것을 막는 것뿐.
        if payload.reflex_action:
            _record_reflex_memory(payload.agent_id, payload.reflex_action)

        # ── danger 게이트 ──────────────────────────────────────────────
        # 임계 미만이면 호감도 감점 대상도 아니다(_apply_hostile_affinity 자체가 danger>=0.5 필터).
        max_danger = max((p.danger_score for p in payload.perceptions), default=0.0)
        if max_danger < SLM_REFLEX_DANGER_THRESHOLD:
            logger.info(
                f"[Main] 비긴급(maxdanger={max_danger:.2f}<{SLM_REFLEX_DANGER_THRESHOLD}) — 통보만. "
                f"npc={payload.agent_id}"
            )
            return _empty_batch_json()

        # ── 통보 전용 처리 ────────────────────────────────────────────
        # 반사 판단은 C++ 척수반사 테이블이 0ms 로 끝냈다(SPEC_reflex_table).
        # 서버는 인지·호감도만 갱신하고 행동은 만들지 않는다 — 후속 행동은
        # replan 플래그가 강제하는 다음 prompt 의 LLM 몫(combat_victory 와 동일 사상).
        #
        # 빈 배치를 돌려주는 것이 핵심이다. 여기서 Mode 를 실어 보내면 C++ 이
        # 방금 올린 Combat 을 되돌린다 — UE5 쪽에도 빈 배치 Mode 스킵 가드가 있지만,
        # 애초에 행동 없는 응답이 모드를 바꾸려 들면 안 된다.
        #
        # 전투 진입 확정 — 다음 replan 이 플래너를 필요로 하기 전에 선제 웜업.
        # replan 훅보다 리드타임이 길어 11s 로드가 완전히 숨을 가능성이 있는 유일 지점.
        spawn_background(_prewarm_core_llm(), label="core-prewarm")

        # shield: WS 끊김으로 이 태스크가 취소돼도 감점 루프는 끝까지 — 일부 perception 만
        # 반영된 채 잘리면 호감도가 어중간하게 남는다.
        await asyncio.shield(_apply_hostile_affinity(payload.agent_id, payload.perceptions))

        logger.info(f"[Main] 긴급 통보 처리 완료(maxdanger={max_danger:.2f}) — 무행동 반환. npc={payload.agent_id}")
        return _empty_batch_json()

    except Exception as e:
        logger.error(f"[Main] _handle_emergency_report 실행 중 치명적 오류: {e}")
        import traceback

        traceback.print_exc()
        return _empty_batch_json()


async def _build_prompt_state(envelope: MessageEnvelope) -> AgentState:
    """prompt Envelope → 그래프 초기 AgentState. payload 파싱·GesPrompt 조립·계획
    캐싱 분기 폴백·world/history 스냅샷을 한데 모은다. (history 는 소비 후 clear)."""
    prompt_payload: PromptPayload = envelope.parse_prompt_payload()

    ges_prompt = GesPrompt(**prompt_payload.model_dump(), timestamp=envelope.timestamp)

    target_npc_from_payload = prompt_payload.target_npc_id or None

    # ── 계획 캐싱 분기 파싱 + 방어 폴백 ────────────────────────────────
    # replan=False 인데 보관 plan 이 없으면(첫 턴/유실) e4b 단독 루프가 줄 컨텍스트가
    # 없으므로 강제로 풀 파이프라인(replan=True)으로 되돌려 plan 을 새로 생성한다.
    requires_replan = prompt_payload.requires_replan
    current_plan = prompt_payload.current_plan
    # current_plan 전체가 없거나, 대상 NPC 미상(None→supervisor 가 "Elara" 기본 사용),
    # 또는 대상 NPC plan 누락 시 강제 재계획 — 다른 NPC plan 오참조 방지.
    # 대소문자 무시 — interface_input 의 plan 주입 조회와 정합(elara vs Elara).
    if not requires_replan and (
        not current_plan
        or not target_npc_from_payload
        or not any(k.lower() == target_npc_from_payload.lower() for k in current_plan)
    ):
        logger.info("[Main] replan=False 이나 대상 NPC plan 없음/미상 → 강제 재계획 폴백(replan=True)")
        requires_replan = True

    # replan 확정 직후 플래너 선제 웜업 — Stage1(3s) 실행 창과 병렬화해 콜드 재로드 부분 완화.
    # 실패해도 기존 콜드 경로 폴백이므로 오류 전파 없음.
    if requires_replan:
        spawn_background(_prewarm_core_llm(), label="core-prewarm")

    async with _world_state_lock:
        # 대상 NPC 자신의 최신 상태만 주입 — 없으면 None(프롬프트에서 "Unknown" 처리).
        world_snap = _cached_world_states.get(target_npc_from_payload.lower()) if target_npc_from_payload else None
    return AgentState(
        vr_context=ges_prompt,
        cached_world_state=world_snap,
        requires_replan=requires_replan,
        current_plan=current_plan,
        npc_plans=None,
        next="",
        current_speaker="",
        natural_context=None,
        raw_responses=None,
        structured_responses=None,
        target_npc=target_npc_from_payload,
        action_batch=None,
        action_batches=None,
        target_npcs=[target_npc_from_payload] if target_npc_from_payload else [],
        rules_retry_count=0,
        msg_id=envelope.msg_id,
        timestamp=envelope.timestamp,
        has_error=False,
        error_msg=None,
    )


def _finalize_prompt_response(result: dict, envelope: MessageEnvelope) -> str:
    """그래프 결과 → ModeActionRequest JSON. ActionBatch 추출(멀티/단일 호환)·plan
    회신 조립. ActionBatch 없으면 빈 배치 JSON."""
    # 멀티 NPC: action_batches 우선, 없으면 단일 action_batch 호환
    action_batches: dict = result.get("action_batches") or {}
    if not action_batches:
        single = result.get("action_batch")
        if single:
            action_batches = {single.AgentID: single}

    if not action_batches:
        logger.warning("[Main] 에이전트가 ActionBatch를 생성하지 않았습니다.")
        return _empty_batch_json()

    logger.info(f"[Main] ActionBatch 생성 완료: {list(action_batches.keys())}")
    first_batch = next(iter(action_batches.values()))

    # 재계획 산출 plan 회신 — replan 시만 채워짐. e4b 단독 응답이면 빈 dict.
    npc_plans = result.get("npc_plans") or {}
    if npc_plans:
        logger.info(f"[Main] NpcPlans 회신: {list(npc_plans.keys())}")

    plan_achieved = result.get("plan_achieved") or {}
    if plan_achieved:
        logger.info(f"[Main] PlanAchieved 회신: {[k for k, v in plan_achieved.items() if v]}")

    wrapper = ModeActionRequest(
        Mode=first_batch.Mode,
        ActionBatches=action_batches,
        NpcPlans=npc_plans,
        PlanAchieved=plan_achieved,
    )
    return wrapper.model_dump_json()


async def _handle_prompt(envelope: MessageEnvelope) -> str:
    """조립(_build_prompt_state) → 그래프 실행 → 응답(_finalize_prompt_response)."""
    logger.info(f"[Main] prompt 처리 시작. msg_id={envelope.msg_id}")

    initial_state = await _build_prompt_state(envelope)

    logger.info("[Main] Graph 비동기 실행 시작...")
    try:
        result = await app_graph.ainvoke(initial_state)
    except Exception as e:
        logger.error(f"[Main] LangGraph 실행 중 치명적 오류: {e}")
        traceback.print_exc()
        return _empty_batch_json()

    return _finalize_prompt_response(result, envelope)


async def _handle_state_update(envelope: MessageEnvelope) -> str:
    try:
        state_payload = envelope.parse_state_update_payload()
        async with _world_state_lock:
            _cached_world_states[state_payload.owner_agent_id.lower()] = state_payload.model_dump()
        logger.debug(
            f"[Main] 월드 상태 캐시 갱신 완료. msg_id={envelope.msg_id}, threat_level={state_payload.threat_level}"
        )

        # 해당 NPC의 관계 데이터를 캐시에서 읽어 응답에 포함
        relations = db_manager.get_relations_from_cache(state_payload.owner_agent_id)

        return json.dumps(
            {
                "status": "cached",
                "msg_id": envelope.msg_id,
                "agent_id": state_payload.owner_agent_id,
                "relations": relations,
            }
        )

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


def _parse_request_gen(payload_raw: dict) -> int:
    # UE5 가 보낸 EQS 요청 세대 번호 — 응답에 그대로 echo. UE5 는 stale 응답 차단에 사용.
    # 비정상 값(문자열 등)이 와도 핸들러가 죽지 않도록 방어 — 0 이면 UE5 가 stale 로 드랍.
    try:
        return int(payload_raw.get("request_gen", 0))
    except (TypeError, ValueError):
        logger.warning(f"[LocationDecision] request_gen 파싱 실패 — 0 으로 폴백: {payload_raw.get('request_gen')!r}")
        return 0


def _location_decision_result(agent_id: str, chosen_id: str, reason: str, request_gen: int) -> str:
    """location_decision_result envelope JSON — Fast-Path·LLM 성공 공통 응답 형식."""
    return json.dumps(
        {
            "type": "location_decision_result",
            "payload": {
                "agent_id": agent_id,
                "chosen_id": chosen_id,
                "reason": reason,
                "request_gen": request_gen,
            },
        }
    )


def _location_decision_fast_path(payload_raw: dict, fallback_reason: str) -> str:
    """LLM 호출 없이 즉시 location_decision_result 를 생성하는 폴백 (Fast-Path).

    WHY 모듈 레벨: 핸들러 내부 오류뿐 아니라 _process_llm_message 의 stale 패킷
    경로에서도 호출된다. generic drop 응답은 UE5 의 location_decision_result
    라우팅에 잡히지 않아 WaitingLLM 이 TacticalLLMTimeout 까지 유지되고, 그동안
    Event Report 게이트(NPCStateComponent)도 함께 막히기 때문 — 폴백 결과를
    돌려줘 즉시 해제한다.
    """
    agent_id = payload_raw.get("agent_id", "unknown")
    candidates_raw = payload_raw.get("candidates", [])
    request_gen = _parse_request_gen(payload_raw)

    if not candidates_raw:
        fallback_id = "OPTIMAL"
        reason_str = "no_candidates"
    else:
        import random

        sorted_candidates = sorted(candidates_raw, key=lambda c: c.get("score", 0), reverse=True)
        top_n = sorted_candidates[:3]
        roll = random.randint(1, 100)

        if roll > 40:  # 60% chance to act rationally
            fallback_id = top_n[0].get("id", "OPTIMAL")
            reason_str = f"Fast-Path (Roll: {roll}): Calmly chose optimal cover"
        else:  # 40% chance to panic
            fallback_id = random.choice(top_n[1:] if len(top_n) > 1 else top_n).get("id", "OPTIMAL")
            reason_str = f"Fast-Path (Roll: {roll}): Panicked! Chose suboptimal cover"

    logger.info(f"[LocationDecision] {reason_str}: {fallback_id} (fallback reason: {fallback_reason})")
    return _location_decision_result(agent_id, fallback_id, reason_str, request_gen)


async def _handle_location_decision(envelope: MessageEnvelope) -> str:
    """
    location_decision 핸들러.
    WHY: C++이 EQS로 후보를 뽑고 스코어링까지 완료한 뒤 최종 카테고리 선택만
         LLM에 위임한다. 전체 좌표 생성 없이 경량 판단만 수행하므로 latency가 짧다.

    응답 형식:
        { "type": "location_decision_result",
          "payload": { "agent_id": "...", "chosen_id": "SAFE_0", "reason": "..." } }
    """

    payload_raw = envelope.payload if isinstance(envelope.payload, dict) else {}
    request_gen = _parse_request_gen(payload_raw)

    try:
        payload = LocationDecisionPayload(**(payload_raw))
        logger.info(f"[LocationDecision] 수신: agent={payload.agent_id}, candidates={len(payload.candidates)}")

        if not payload.candidates:
            return _location_decision_fast_path(payload_raw, "no_candidates")

        valid_ids = {c.id for c in payload.candidates}

        # Few-shot raw 프롬프트 — gemma e4b thinking 우회.
        # 직접 측정 (2026-05-16): chat template thinking ~2200ms/289토큰 → raw few-shot ~400ms/4토큰.
        candidate_ids = ", ".join(c.id for c in payload.candidates)
        prompt = _LOCATION_DECISION_PROMPT.format(
            context=payload.context_summary,
            candidate_ids=candidate_ids,
        )

        raw_text, _llm_ms = await _ollama_raw_generate(prompt)
        logger.info(f"[LocationDecision] LLM {_llm_ms:.0f}ms raw={raw_text!r} (gen={request_gen})")

        tokens = raw_text.split()
        if not tokens:
            logger.warning(f"[LocationDecision] LLM 빈 응답 agent={payload.agent_id} → Fast-Path")
            return _location_decision_fast_path(payload_raw, "empty_llm_response")
        text = tokens[0].upper()

        # 유효한 ID인지 검증 (대소문자 무시)
        chosen_id = next((vid for vid in valid_ids if vid.upper() == text), None)

        if not chosen_id:
            logger.warning(f"[LocationDecision] LLM 응답 '{text}'이 유효한 ID 아님 → Fast-Path")
            return _location_decision_fast_path(payload_raw, "invalid_llm_response")

        reason = f"LLM chose {chosen_id} ({payload.context_summary})"
        logger.info(f"[LocationDecision] 결과: agent={payload.agent_id} chosen={chosen_id}")

        return _location_decision_result(payload.agent_id, chosen_id, reason, request_gen)

    except Exception as e:
        logger.error(f"[LocationDecision] 오류: {e}\n{traceback.format_exc()}")
        return _location_decision_fast_path(payload_raw, "exception")


# ─────────────────────────────────────────────────────────────────────────────
# Debug Dashboard
# ─────────────────────────────────────────────────────────────────────────────

_STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")
_DEBUG_HTML_PATH = os.path.join(_STATIC_DIR, "debug.html")
_TEST_CHAT_HTML_PATH = os.path.join(_STATIC_DIR, "test_chat.html")
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
                results.append(
                    {
                        "npc_id": data.get("name", fname[:-5]),
                        "file_path": fpath,
                        "folder": subdir,
                        "role": data.get("role", ""),
                        "importance": data.get("importance", "normal"),
                        "traits": data.get("traits", []),
                    }
                )
            except Exception as e:
                # persona YAML 파싱 실패를 무음 통과시키면 디버그 대시보드에서
                # NPC 가 조용히 누락됨 — 원인 파일·사유 로깅
                logger.warning(f"[Debug] persona 로드 실패 — {fpath}: {e}")
    return results


# 디버그 페이지는 개발 중 자주 바뀌는데 브라우저가 캐시하면 구 JS 가 남아 없어진
# 엔드포인트로 계속 쏜다(2026-09-05 실측: 캐시된 페이지가 구 /api/debug/prompt 를 호출해
# 인벤토리 없는 프롬프트가 나감). 캐시를 원천 차단한다.
_NO_STORE = {"Cache-Control": "no-store, max-age=0"}


@app.get("/debug", response_class=HTMLResponse)
async def debug_dashboard():
    if os.path.exists(_DEBUG_HTML_PATH):
        with open(_DEBUG_HTML_PATH, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read(), headers=_NO_STORE)
    return HTMLResponse(content="<h1>debug.html not found</h1>", status_code=404)


@app.get("/test_chat", response_class=HTMLResponse)
async def test_chat_page():
    """UE5 없이 NPC 대화 파이프라인 테스트 — 브라우저 채팅 UI."""
    if os.path.exists(_TEST_CHAT_HTML_PATH):
        with open(_TEST_CHAT_HTML_PATH, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read(), headers=_NO_STORE)
    return HTMLResponse(content="<h1>test_chat.html not found</h1>", status_code=404)


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
    return {
        "status": "ok",
        "source_id": rel.source_id,
        "target_id": rel.target_id,
        "affinity_score": rel.affinity_score,
        "reputation_tag": rel.reputation_tag,
    }


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
        llm_model = "gemma4-12b"
    elif req.importance == "high":
        llm_model = "qwen3:8b"
    else:
        llm_model = "gemma4:e4b"
    return {
        "status": "ok",
        "npc_id": npc_id,
        "importance": req.importance,
        "llm_model": llm_model,
    }


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
            Actions=[
                GameAction(
                    ActionType=req.action_type,
                    FacialState=req.facial,
                    Parameters={k: str(v) for k, v in req.params.items()},
                )
            ],
        )
        wrapper = ModeActionRequest(Mode=req.mode, ActionBatches={npc_id: batch})
        async with _ws_send_lock:
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
    # 테스트용 모의 인벤토리 — UE5 없이 NPC 보유 아이템 시뮬레이트. [{id,name,count}, ...].
    npc_inventory: Optional[list] = None


# 디버그 경로 plan 캐시 — UE5 NPCStateComponent 의 plan 보관/ShouldReplan 을 흉내.
# npc_id → plan dict. 없던 것: requires_replan/current_plan 미동봉이라 PromptPayload
# 기본값(True)으로 매 턴 12B 풀 파이프라인이 돌았음 (UE5 실동작과 다른 프로파일).
# plan 보유 시 requires_replan=False + current_plan 동봉(e4b 경량 루프), 응답 NpcPlans 로
# 갱신, PlanAchieved=True 면 삭제 → 다음 턴 재계획. combat 최초 전환 트리거는 미시뮬(단순화).
_debug_plan_cache: dict = {}


@app.post("/api/debug/say")
async def api_debug_say(req: DebugPromptRequest):
    """디버그: 브라우저에서 친 말을 UE5 로 넘겨 마이크와 동일한 경로로 처리시킨다.

    서버가 직접 그래프를 돌리지 않는 이유 — NPC 인벤토리·valid_targets·주변 가구·plan
    캐시는 전부 UE5 가 prompt 마다 조립해 보내는 값이다. 서버가 이를 흉내내면 실제
    게임과 다른 입력으로 검증하게 되고, 특히 인벤토리가 비어 "그거 없다"만 나온다.
    전사 텍스트만 넘기면 UE5 가 SendPlayerDialogue 로 평소 prompt 를 쏘므로 그 뒤는
    마이크 경로와 완전히 같다 — 응답 ActionBatch 도 정상 WS 경로로 돌아가 게임에서 실행된다.

    그래서 결과는 이 응답에 담기지 않는다. 게임 화면·UE5 로그에서 확인할 것.
    """
    if _active_llm_ws is None:
        raise HTTPException(
            status_code=409,
            detail="UE5 미연결 — PIE 를 켜고 다시 시도하세요. UE5 없이 파이프라인만 볼 거라면 /test_chat 을 쓰세요.",
        )

    msg = json.dumps(
        {
            "type": "debug_prompt",
            "npc_id": req.npc_id,
            "player_id": req.player_id,
            "text": req.text,
        },
        ensure_ascii=False,
    )
    try:
        async with _ws_send_lock:
            await _active_llm_ws.send_text(msg)
    except Exception as e:  # noqa: BLE001
        logger.warning(f"[Debug] UE5 전송 실패: {e}")
        raise HTTPException(status_code=502, detail=f"UE5 전송 실패: {e}")

    logger.info(f"[Debug] UE5 로 전달 — {req.player_id} → {req.npc_id}: \"{req.text}\"")
    return {"status": "dispatched", "npc_id": req.npc_id, "text": req.text}


@app.post("/api/debug/prompt")
async def api_debug_prompt(req: DebugPromptRequest):
    """디버그: UE 없이 콘솔/웹에서 NPC 에게 직접 말 걸기.
    PROMPT envelope 를 만들어 그래프 실행 → ActionBatch(JSON) 반환."""
    import uuid
    import time as _t

    cached_plan = _debug_plan_cache.get(req.npc_id)
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
            # 모의 인벤토리를 npc_id 키로 래핑 (PromptPayload.npc_inventory 구조와 정합).
            "npc_inventory": {req.npc_id: req.npc_inventory} if req.npc_inventory else None,
            # plan 캐싱 시뮬레이션 (UE5 SendPlayerDialogue 의 분기와 정합).
            "requires_replan": cached_plan is None,
            "current_plan": {req.npc_id: cached_plan} if cached_plan else None,
        },
    )
    try:
        result_json = await _handle_prompt(env)
        result = json.loads(result_json)
        # plan 갱신 → 다음 디버그 턴은 e4b 경량 루프. 달성 시 삭제 → 다음 턴 재계획.
        for npc_id, plan in (result.get("NpcPlans") or {}).items():
            _debug_plan_cache[npc_id] = plan
        for npc_id, achieved in (result.get("PlanAchieved") or {}).items():
            if achieved:
                _debug_plan_cache.pop(npc_id, None)
                logger.info(f"[Debug] plan 달성 → 캐시 제거: {npc_id} (다음 턴 재계획)")
        return result
    except Exception as e:  # noqa: BLE001
        logger.exception(f"[Debug] prompt 처리 실패: {e}")
        raise HTTPException(status_code=500, detail=str(e))


