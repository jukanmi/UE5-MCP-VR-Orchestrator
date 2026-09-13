"""
File: debug_routes.py
Role: 디버그 대시보드(/debug, /test_chat)와 운영 REST(/api/*) — main.py 의 WS 파이프라인과 분리.

UE5 소켓·송신 락은 server_state.STATE 로 공유한다. 그래프 실행(_handle_prompt)은 main.py 소유라
build_router(handle_prompt) 로 주입받는다 — debug_routes 가 main 을 import 하면 순환이 된다.
"""

import json
import logging
import os
import time
import uuid
from typing import Awaitable, Callable, Optional

import yaml
from fastapi import APIRouter, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

from .schemas.actions import ActionBatch, GameAction, ModeActionRequest
from .schemas.envelope import EEnvelopeType, MessageEnvelope
from .server_state import STATE
from .utils import db_manager, llm_factory

logger = logging.getLogger("api")

router = APIRouter()

# main.py 가 build_router 로 채운다.
handle_prompt: Callable[[MessageEnvelope], Awaitable[str]]


def build_router(handle_prompt_fn: Callable[[MessageEnvelope], Awaitable[str]]) -> APIRouter:
    global handle_prompt
    handle_prompt = handle_prompt_fn
    return router


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
                        "llm_model": llm_factory.model_for_importance(data.get("importance", "normal")),
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


@router.get("/debug", response_class=HTMLResponse)
async def debug_dashboard():
    if os.path.exists(_DEBUG_HTML_PATH):
        with open(_DEBUG_HTML_PATH, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read(), headers=_NO_STORE)
    return HTMLResponse(content="<h1>debug.html not found</h1>", status_code=404)


@router.get("/test_chat", response_class=HTMLResponse)
async def test_chat_page():
    """UE5 없이 NPC 대화 파이프라인 테스트 — 브라우저 채팅 UI."""
    if os.path.exists(_TEST_CHAT_HTML_PATH):
        with open(_TEST_CHAT_HTML_PATH, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read(), headers=_NO_STORE)
    return HTMLResponse(content="<h1>test_chat.html not found</h1>", status_code=404)


@router.get("/api/affinity")
async def api_get_affinity():
    rows = await db_manager.get_all_affinity()
    return {"rows": rows}


class AffinitySetRequest(BaseModel):
    source_id: str
    target_id: str
    score: int
    note: str = "debug_override"


@router.post("/api/affinity")
async def api_set_affinity(req: AffinitySetRequest):
    rel = await db_manager.set_affinity_direct(req.source_id, req.target_id, req.score, req.note)
    return {
        "status": "ok",
        "source_id": rel.source_id,
        "target_id": rel.target_id,
        "affinity_score": rel.affinity_score,
        "reputation_tag": rel.reputation_tag,
    }


@router.delete("/api/affinity")
async def api_delete_affinity(source_id: str, target_id: str):
    await db_manager.delete_affinity(source_id, target_id)
    return {"status": "deleted"}


@router.get("/api/npcs")
async def api_get_npcs():
    return {"npcs": _list_personas()}


class ImportanceUpdateRequest(BaseModel):
    importance: str  # "normal" | "high" | "core"


@router.put("/api/npcs/{npc_id}/importance")
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

    return {
        "status": "ok",
        "npc_id": npc_id,
        "importance": req.importance,
        "llm_model": llm_factory.model_for_importance(req.importance),
    }


class NpcCommandRequest(BaseModel):
    action_type: str
    mode: str = "Common"
    facial: str = "Neutral"
    params: dict = {}


@router.post("/api/npc/{npc_id}/command")
async def api_npc_command(npc_id: str, req: NpcCommandRequest):
    if STATE.active_llm_ws is None:
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
        await STATE.send_to_ue(wrapper.model_dump_json())
        logger.info(f"[Debug] NPC 명령 전송: {npc_id} → {req.action_type}")
        return {"status": "ok", "npc_id": npc_id, "action": req.action_type}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/api/ws/status")
async def api_ws_status():
    return {"connected": STATE.active_llm_ws is not None}


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


@router.post("/api/debug/say")
async def api_debug_say(req: DebugPromptRequest):
    """디버그: 브라우저에서 친 말을 UE5 로 넘겨 마이크와 동일한 경로로 처리시킨다.

    서버가 직접 그래프를 돌리지 않는 이유 — NPC 인벤토리·valid_targets·주변 가구·plan
    캐시는 전부 UE5 가 prompt 마다 조립해 보내는 값이다. 서버가 이를 흉내내면 실제
    게임과 다른 입력으로 검증하게 되고, 특히 인벤토리가 비어 "그거 없다"만 나온다.
    전사 텍스트만 넘기면 UE5 가 SendPlayerDialogue 로 평소 prompt 를 쏘므로 그 뒤는
    마이크 경로와 완전히 같다 — 응답 ActionBatch 도 정상 WS 경로로 돌아가 게임에서 실행된다.

    그래서 결과는 이 응답에 담기지 않는다. 게임 화면·UE5 로그에서 확인할 것.
    """
    if STATE.active_llm_ws is None:
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
        await STATE.send_to_ue(msg)
    except Exception as e:  # noqa: BLE001
        logger.warning(f"[Debug] UE5 전송 실패: {e}")
        raise HTTPException(status_code=502, detail=f"UE5 전송 실패: {e}")

    logger.info(f"[Debug] UE5 로 전달 — {req.player_id} → {req.npc_id}: \"{req.text}\"")
    return {"status": "dispatched", "npc_id": req.npc_id, "text": req.text}


@router.post("/api/debug/prompt")
async def api_debug_prompt(req: DebugPromptRequest):
    """디버그: UE 없이 콘솔/웹에서 NPC 에게 직접 말 걸기.
    PROMPT envelope 를 만들어 그래프 실행 → ActionBatch(JSON) 반환."""

    cached_plan = _debug_plan_cache.get(req.npc_id)
    env = MessageEnvelope(
        msg_id=str(uuid.uuid4()),
        # auth_token 은 WS 수신 루프에서만 검증됨. 디버그는 handle_prompt 직접 호출이라
        # 검증을 거치지 않지만 pydantic 필수 필드라 env 값(없으면 더미)으로 채운다.
        auth_token=os.environ.get("WS_AUTH_TOKEN", "debug"),
        timestamp=time.time(),
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
        result_json = await handle_prompt(env)
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
