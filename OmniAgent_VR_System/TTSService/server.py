"""
File: OmniAgent_VR_System/TTSService/server.py
Role: M1 스텁 TTS 서비스 (FastAPI + WebSocket).

WHY:
  - 실제 VibeVoice 도입(M2) 전, 명세서(§5.3, vibevoice-dev-spec §6) API 규격을
    그대로 노출하는 더미 서버로 UE5↔Orchestrator↔TTS end-to-end 파이프라인을
    검증한다.
  - 본 스텁은 GPU 없이 동작하며, 440Hz 사인파를 pcm_s16le 청크로 스트리밍한다.

PROTOCOL:
  - POST /v1/tts/synthesize
      body: { text, voice_id, emotion?, speaking_rate?, pitch?, sample_rate? }
      resp: { request_id, ws_url }
  - GET  /ws/tts/stream/{request_id}
      서버가 연결 즉시 다음 메시지를 순서대로 push:
        { type:"audio_chunk", request_id, sequence, sample_rate, channels,
          audio_base64, is_last:false }
        ...
        { type:"completed", request_id, total_duration_ms }

NOTES:
  - 청크 포맷: pcm_s16le, 16kHz mono (TTS 통합 계획서 §9 결정).
  - 청크 길이: 100ms (= 1600 sample) → 부드러운 스트리밍 체감.
  - 길이는 텍스트 길이에 비례(글자당 60ms, 최소 800ms, 최대 4000ms).

RUN:
  python -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001
"""
from __future__ import annotations

import asyncio
import base64
import math
import time
import uuid
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, Field

# ─────────────────────────────────────────────────────────────────────────────
# Config
# ─────────────────────────────────────────────────────────────────────────────
SAMPLE_RATE_DEFAULT = 16000
CHUNK_MS = 100
CHANNELS = 1
TONE_HZ = 440.0
MIN_DURATION_MS = 800
MAX_DURATION_MS = 4000
MS_PER_CHAR = 60


app = FastAPI(title="TTSService-Stub", version="0.1.0")

# request_id → 합성 메타정보 보관. 실제 모델 호출 없으므로 텍스트/옵션만 캐싱.
_pending: dict[str, "SynthesizeRequest"] = {}


# ─────────────────────────────────────────────────────────────────────────────
# Schemas (vibevoice-dev-spec §6.1과 호환)
# ─────────────────────────────────────────────────────────────────────────────
class SynthesizeRequest(BaseModel):
    text: str
    voice_id: str = "default"
    emotion: str = "neutral"
    speaking_rate: float = 1.0
    pitch: float = 0.0
    sample_rate: int = SAMPLE_RATE_DEFAULT
    output_format: str = "pcm_s16le"


class SynthesizeResponse(BaseModel):
    request_id: str
    ws_url: str
    sample_rate: int
    channels: int = CHANNELS


# ─────────────────────────────────────────────────────────────────────────────
# REST: 합성 요청 등록
# ─────────────────────────────────────────────────────────────────────────────
@app.post("/v1/tts/synthesize", response_model=SynthesizeResponse)
async def synthesize(req: SynthesizeRequest) -> SynthesizeResponse:
    request_id = f"tts_{uuid.uuid4().hex[:12]}"
    _pending[request_id] = req
    return SynthesizeResponse(
        request_id=request_id,
        ws_url=f"ws://127.0.0.1:8001/ws/tts/stream/{request_id}",
        sample_rate=req.sample_rate,
    )


# ─────────────────────────────────────────────────────────────────────────────
# WebSocket: 청크 스트리밍
# ─────────────────────────────────────────────────────────────────────────────
@app.websocket("/ws/tts/stream/{request_id}")
async def ws_stream(websocket: WebSocket, request_id: str) -> None:
    await websocket.accept()
    req = _pending.pop(request_id, None)
    if req is None:
        await websocket.send_json({
            "type": "error",
            "request_id": request_id,
            "code": "NOT_FOUND",
            "message": "request_id 가 존재하지 않거나 이미 소비됨",
            "retryable": False,
        })
        await websocket.close()
        return

    duration_ms = max(MIN_DURATION_MS, min(MAX_DURATION_MS, len(req.text) * MS_PER_CHAR))
    sample_rate = req.sample_rate
    samples_per_chunk = sample_rate * CHUNK_MS // 1000
    total_chunks = max(1, duration_ms // CHUNK_MS)
    started_at = time.perf_counter()

    try:
        sequence = 0
        phase = 0.0
        phase_step = 2.0 * math.pi * TONE_HZ / sample_rate

        while sequence < total_chunks:
            pcm = bytearray(samples_per_chunk * 2)
            for i in range(samples_per_chunk):
                # 0.3 진폭, 끝부분 200ms fade-out → 클릭 방지
                amp = 0.3
                tail_start = total_chunks - 2
                if sequence >= tail_start:
                    fade_pos = (sequence - tail_start) * samples_per_chunk + i
                    fade_len = 2 * samples_per_chunk
                    amp *= max(0.0, 1.0 - fade_pos / fade_len)
                value = int(math.sin(phase) * amp * 32767)
                # little-endian s16
                pcm[i * 2] = value & 0xFF
                pcm[i * 2 + 1] = (value >> 8) & 0xFF
                phase += phase_step
                if phase > 2.0 * math.pi:
                    phase -= 2.0 * math.pi

            is_last = sequence == total_chunks - 1
            await websocket.send_json({
                "type": "audio_chunk",
                "request_id": request_id,
                "sequence": sequence,
                "sample_rate": sample_rate,
                "channels": CHANNELS,
                "audio_base64": base64.b64encode(bytes(pcm)).decode("ascii"),
                "is_last": is_last,
            })
            sequence += 1
            # 실시간 송출 페이싱 (스트리밍 체감)
            await asyncio.sleep(CHUNK_MS / 1000.0)

        elapsed_ms = int((time.perf_counter() - started_at) * 1000)
        await websocket.send_json({
            "type": "completed",
            "request_id": request_id,
            "total_duration_ms": elapsed_ms,
        })
    except WebSocketDisconnect:
        # 클라이언트가 중간에 끊은 경우 — 조용히 종료
        return
    finally:
        try:
            await websocket.close()
        except Exception:
            pass


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok", "service": "tts-stub"}
