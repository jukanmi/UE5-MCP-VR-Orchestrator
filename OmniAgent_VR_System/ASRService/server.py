"""
File: OmniAgent_VR_System/ASRService/server.py
Role: ASR(Automatic Speech Recognition) 서비스 — 스텁(echo) 단계.

WHY:
  - 채팅 위젯 제거 후 플레이어 텍스트 입력 수단이 없음.
  - faster-whisper 통합 전에 UE5 클라이언트·프로토콜·CognitiveEngine 연동 경로를
    먼저 깔아두기 위한 더미 서비스.

PROTOCOL (M1 스텁):
  - WS endpoint: ws://127.0.0.1:8002/ws/asr/stream
  - 클라이언트가 먼저 JSON 으로 start 메시지 송신:
        { "type": "start", "request_id": "...", "target_npc_id": "Skadi",
          "player_id": "Player_1", "sample_rate": 16000, "language": "KR" }
  - 이후 binary 프레임으로 pcm_s16le 16kHz mono 청크 전송 (TTS audio_chunk 와 대칭).
  - 클라이언트가 JSON 으로 end 메시지 송신:
        { "type": "end" }
  - 서버는 수신 종료 후 final transcript 반환:
        { "type": "final", "request_id": "...", "target_npc_id": "Skadi",
          "transcript": "[ASR stub] 1.23s 받음", "duration_ms": 1234 }
  - (M2) partial 결과는 청크 처리하며 중간 송신 예정.

M2 계획:
  - faster-whisper 통합 — 동일 protocol 유지, transcript 가 실제 인식 결과.
  - REST forward 옵션: server 측에서 직접 CognitiveEngine /api/debug/prompt 로 전달.
"""
from __future__ import annotations

import asyncio
import json
import logging
import time
import uuid
from contextlib import asynccontextmanager
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

logger = logging.getLogger("asr")
logger.setLevel(logging.INFO)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s | %(message)s",
    datefmt="%H:%M:%S",
)

TARGET_SAMPLE_RATE = 16000
BYTES_PER_SAMPLE = 2  # pcm_s16le


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("[ASR] 스텁 서비스 기동 — Whisper 미로드, 더미 echo 만 응답")
    yield


app = FastAPI(title="ASRService-Stub", version="0.1.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://127.0.0.1:8000", "http://localhost:8000"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
async def health() -> dict:
    return {"status": "ok", "service": "asr-stub", "version": "0.1.0"}


@app.websocket("/ws/asr/stream")
async def ws_stream(websocket: WebSocket) -> None:
    """ASR 스트리밍 WS — 더미 echo 단계.

    프로토콜은 docstring 참조. 본 스텁은 audio 청크 자체는 디코딩하지 않고
    수신 바이트 수와 청크 수만 추적해 final transcript 에 반환한다.
    """
    await websocket.accept()
    request_id = f"asr_{uuid.uuid4().hex[:12]}"
    target_npc_id: Optional[str] = None
    player_id: str = "Player_1"
    sample_rate: int = TARGET_SAMPLE_RATE
    language: Optional[str] = None

    started_at: Optional[float] = None
    total_bytes = 0
    chunk_count = 0

    try:
        while True:
            msg = await websocket.receive()
            # FastAPI WebSocket.receive() — text/bytes/close 구분
            if "text" in msg and msg["text"] is not None:
                try:
                    data = json.loads(msg["text"])
                except json.JSONDecodeError:
                    await websocket.send_json({
                        "type": "error", "request_id": request_id,
                        "code": "BAD_JSON", "message": msg["text"][:80],
                    })
                    continue

                mtype = data.get("type")
                if mtype == "start":
                    request_id = data.get("request_id") or request_id
                    target_npc_id = data.get("target_npc_id")
                    player_id = data.get("player_id") or player_id
                    sample_rate = int(data.get("sample_rate") or TARGET_SAMPLE_RATE)
                    language = data.get("language")
                    started_at = time.perf_counter()
                    logger.info(
                        f"[ASR] start request_id={request_id} target={target_npc_id} "
                        f"sr={sample_rate} lang={language}"
                    )
                    await websocket.send_json({
                        "type": "ready", "request_id": request_id,
                    })
                elif mtype == "end":
                    elapsed_ms = int(((time.perf_counter() - started_at) * 1000) if started_at else 0)
                    duration_audio_ms = int(total_bytes / (sample_rate * BYTES_PER_SAMPLE) * 1000)
                    stub_text = f"[ASR stub] 청크 {chunk_count}개 · 오디오 {duration_audio_ms}ms · 수신 {total_bytes}B"
                    logger.info(
                        f"[ASR] end request_id={request_id} "
                        f"bytes={total_bytes} chunks={chunk_count} audio_ms={duration_audio_ms} "
                        f"elapsed_ms={elapsed_ms}"
                    )
                    await websocket.send_json({
                        "type": "final",
                        "request_id": request_id,
                        "target_npc_id": target_npc_id,
                        "player_id": player_id,
                        "transcript": stub_text,
                        "duration_ms": duration_audio_ms,
                        "language": language or "KR",
                    })
                    break
                else:
                    await websocket.send_json({
                        "type": "error", "request_id": request_id,
                        "code": "UNKNOWN_TYPE", "message": str(mtype),
                    })
            elif "bytes" in msg and msg["bytes"] is not None:
                # pcm 청크 — 스텁은 카운트만
                chunk = msg["bytes"]
                total_bytes += len(chunk)
                chunk_count += 1
            elif msg.get("type") == "websocket.disconnect":
                logger.info(f"[ASR] 클라이언트 연결 종료 request_id={request_id}")
                break
    except WebSocketDisconnect:
        logger.info(f"[ASR] WebSocketDisconnect request_id={request_id}")
    except Exception as e:
        logger.exception(f"[ASR] 스트림 처리 실패: {e}")
        try:
            await websocket.send_json({
                "type": "error", "request_id": request_id,
                "code": "INTERNAL", "message": str(e),
            })
        except Exception:
            pass
    finally:
        try:
            await websocket.close()
        except Exception:
            pass


# ─────────────────────────────────────────────────────────────────────────────
# 보조 REST — 디버깅용 단발 transcript 주입 (UE5 마이크 통합 전 폴백)
# ─────────────────────────────────────────────────────────────────────────────
class StubTranscribeRequest(BaseModel):
    text: str
    target_npc_id: Optional[str] = None
    player_id: str = "Debug_Browser"


@app.post("/api/asr/stub_transcribe")
async def api_stub_transcribe(req: StubTranscribeRequest) -> dict:
    """오디오 없이 임의 transcript 를 final 형식으로 반환. CognitiveEngine 폴백 통로."""
    return {
        "type": "final",
        "request_id": f"asr_{uuid.uuid4().hex[:12]}",
        "target_npc_id": req.target_npc_id,
        "player_id": req.player_id,
        "transcript": req.text,
        "duration_ms": 0,
        "language": "KR",
    }
