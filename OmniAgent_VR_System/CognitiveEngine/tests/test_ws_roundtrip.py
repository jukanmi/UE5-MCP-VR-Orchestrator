"""수동 라이브 WS 왕복 스모크 — fake UE5 클라이언트 (pytest 유닛 아님, test_ 함수 없음).

UE5 없이 envelope 계약을 실 서버 왕복으로 검증한다:
  prompt 전송 → ModeActionRequest 수신 → 스키마·EAction·Parameters·valid_targets 검사.
Memo 의 "실 UE5 왕복 미검증" 항목을 PIE 없이 커버하는 용도.

전제: Server.bat(또는 Dev.bat) 로 서버 구동 + Ollama 기동 (Live LLM 호출 — 수 초~수십 초).
접속 정보는 UE5 와 동일하게 Config/DefaultGame.ini [OmniAgent] 에서 읽는다.

실행: CognitiveEngine 디렉토리에서 (파일 직접 실행 — `-m tests.…` 는 site-packages 의
잡패키지 `tests` 가 로컬을 가려 불가, 2026-07-07 확인)
  python tests/test_ws_roundtrip.py                 # 풀 파이프라인(requires_replan=True)
  python tests/test_ws_roundtrip.py --light         # e4b 단독 경량 루프
  python tests/test_ws_roundtrip.py --say "대사"    # 발화 교체
"""

import argparse
import asyncio
import configparser
import json
import sys
import time
import uuid
from pathlib import Path
from typing import get_args

sys.path.append(str(Path(__file__).resolve().parents[1]))

import websockets

from app.schemas.actions import EAction, ModeActionRequest, NPCFacialState
from app.schemas.envelope import PromptPayload

REPO_ROOT = Path(__file__).resolve().parents[3]
GAME_INI = REPO_ROOT / "Config" / "DefaultGame.ini"

RECEIVE_TIMEOUT_S = 90.0  # 12B cold-start 포함 여유
VALID_TARGETS = ["Player", "Self", "Enemy", "Elara"]


def _load_conn_info() -> tuple:
    """UE5 와 동일 소스(DefaultGame.ini [OmniAgent])에서 접속 정보 로드."""
    cp = configparser.ConfigParser()
    cp.read(GAME_INI, encoding="utf-8")
    sec = cp["OmniAgent"]
    return sec["AuthToken"], sec.get("ServerHost", "127.0.0.1"), sec.get("LLMPort", "8000")


def _build_prompt_envelope(auth_token: str, transcript: str, requires_replan: bool) -> tuple:
    payload = PromptPayload(
        player_id="Player1",
        voice_transcript=transcript,
        target_npc_id="Elara",
        # GestureData 스키마(vr_context.py) — hand 필수.
        gestures=[{"gesture_type": "Point", "hand": "Right", "confidence": 0.9, "target_entity_id": "Enemy"}],
        player_location={"x": 100.0, "y": 200.0, "z": 0.0},
        requires_replan=requires_replan,
        npc_inventory={"Elara": [{"id": "potion_small", "name": "Small Potion", "count": 1}]},
        valid_targets=VALID_TARGETS,
    )
    msg_id = f"smoke-{uuid.uuid4()}"
    envelope = {
        "protocol_version": 1,
        "msg_id": msg_id,
        "ref_msg_id": None,
        "auth_token": auth_token,
        "timestamp": time.time(),
        "type": "prompt",
        "payload": payload.model_dump(),
    }
    return json.dumps(envelope, ensure_ascii=False), msg_id


def _validate_batch(raw: str) -> list:
    """ModeActionRequest 계약 검사. 위반 메시지 리스트 반환(빈 리스트 = 통과)."""
    problems = []
    req = ModeActionRequest(**json.loads(raw))  # pydantic 이 Mode/구조 강제
    valid_actions = set(get_args(EAction))
    valid_facial = set(get_args(NPCFacialState))
    n_actions = 0
    for agent_id, batch in req.ActionBatches.items():
        if batch.AgentID != agent_id:
            problems.append(f"AgentID 불일치: 키={agent_id} vs batch.AgentID={batch.AgentID}")
        for act in batch.Actions:
            n_actions += 1
            if act.ActionType not in valid_actions:
                problems.append(f"{agent_id}: 비유효 ActionType {act.ActionType}")
            if act.FacialState not in valid_facial:
                problems.append(f"{agent_id}: 비유효 FacialState {act.FacialState}")
            for k, v in act.Parameters.items():
                if not isinstance(v, str):
                    problems.append(f"{agent_id}/{act.ActionType}: Parameters[{k}] 가 str 아님({type(v).__name__})")
                if k != k.lower():
                    problems.append(f"{agent_id}/{act.ActionType}: Parameters 키 {k} snake_case 위반")
            tid = act.Parameters.get("target_id")
            if tid and tid not in VALID_TARGETS:
                # Stage1 target enum grammar 가 강제하는 계약 — 폴백 경로에선 자유문자열
                # 가능하므로 실패가 아니라 경고로 노출.
                print(f"  [warn] {agent_id}/{act.ActionType}: target_id '{tid}' 가 valid_targets 밖 (폴백 경로?)")
        print(f"  batch {agent_id}: Mode={req.Mode}, actions={[a.ActionType for a in batch.Actions]}")
    if n_actions == 0:
        print("  [warn] 액션 0개 (순수 대화 턴이면 정상)")
    return problems


async def run(transcript: str, requires_replan: bool) -> int:
    auth_token, host, port = _load_conn_info()
    url = f"ws://{host}:{port}/ws/llm"
    msg, msg_id = _build_prompt_envelope(auth_token, transcript, requires_replan)
    print(f"[Smoke] 접속 {url} (replan={requires_replan})")
    try:
        ws = await websockets.connect(url)
    except OSError as e:
        print(f"[Smoke] 서버 접속 실패 — Server.bat/Dev.bat 구동 확인: {e}")
        return 1

    async with ws:
        t0 = time.perf_counter()
        await ws.send(msg)
        print(f"[Smoke] prompt 전송 msg_id={msg_id}: {transcript!r}")

        # ModeActionRequest 가 올 때까지 수신 — 다른 비동기 메시지는 스킵.
        deadline = t0 + RECEIVE_TIMEOUT_S
        while True:
            remaining = deadline - time.perf_counter()
            if remaining <= 0:
                print(f"[Smoke] FAIL — {RECEIVE_TIMEOUT_S}s 내 ModeActionRequest 미수신")
                return 1
            raw = await asyncio.wait_for(ws.recv(), timeout=remaining)
            data = json.loads(raw)
            if "error" in data:
                print(f"[Smoke] FAIL — 서버 에러 응답: {data}")
                return 1
            if data.get("status") == "dropped":
                print(f"[Smoke] FAIL — 패킷 드랍: {data}")
                return 1
            if "ActionBatches" in data:
                break
            print(f"[Smoke] 비대상 메시지 스킵: type={data.get('type', '?')}")

        elapsed = time.perf_counter() - t0
        print(f"[Smoke] ModeActionRequest 수신 ({elapsed:.1f}s):")
        problems = _validate_batch(raw)

    if problems:
        print("[Smoke] FAIL — 계약 위반:")
        for p in problems:
            print(f"  - {p}")
        return 1
    print("[Smoke] PASS — envelope 왕복·ActionBatch 계약 OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Live WS roundtrip smoke (fake UE5 client)")
    ap.add_argument("--say", default="엘라라, 저기 적이 나타났어! 나를 도와줘!", help="voice_transcript")
    ap.add_argument("--light", action="store_true", help="requires_replan=False (e4b 경량 루프)")
    args = ap.parse_args()
    return asyncio.run(run(args.say, requires_replan=not args.light))


if __name__ == "__main__":
    sys.exit(main())
