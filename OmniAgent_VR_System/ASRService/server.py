"""
File: OmniAgent_VR_System/ASRService/server.py
Role: ASR(Automatic Speech Recognition) 서비스 — faster-whisper 실인식 (M2).

WHY:
  - 채팅 위젯 제거 후 플레이어 텍스트 입력 수단이 없음 → 마이크 음성을 텍스트로.
  - UE5 VoiceInputComponent 가 push-to-talk 로 PCM 을 스트리밍 → 본 서비스가 인식 →
    final transcript 회신 → UE5 가 SendPlayerDialogue 로 CognitiveEngine 에 전달.

PROTOCOL:
  - WS endpoint: ws://127.0.0.1:8002/ws/asr/stream
  - 클라이언트 start(JSON):
        { "type": "start", "request_id": "...", "target_npc_id": "Skadi",
          "player_id": "Player_1", "sample_rate": 48000, "language": "KR" }
  - 이후 binary 프레임으로 pcm_s16le mono 청크 (네이티브 레이트, sample_rate 에 명시).
  - 클라이언트 end(JSON): { "type": "end" }
  - 서버 final(JSON):
        { "type": "final", "request_id": "...", "target_npc_id": "Skadi",
          "transcript": "<인식 결과>", "duration_ms": 1234, "language": "KR" }

설계 결정(§0):
  - 모델: large-v3 / device=cuda / compute=float16 (한국어 품질 우선).
  - 리샘플: UE 는 네이티브 레이트로 보내고, 여기서 soxr 로 16kHz 변환(whisper 입력 규격).
  - partial(중간 결과)은 미구현 — end 시 final 단발. (M3 후보)
"""
from __future__ import annotations

import asyncio
import json
import logging
import time
import uuid
from contextlib import asynccontextmanager
from typing import Optional

import numpy as np
import soxr
from faster_whisper import WhisperModel

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

TARGET_SAMPLE_RATE = 16000   # whisper 입력 규격
BYTES_PER_SAMPLE = 2         # pcm_s16le
# end 누락 클라이언트의 무한 스트리밍 → OOM/DoS 방지. 48kHz·16bit·120초 ≈ 11.5MB 상한.
MAX_AUDIO_BUF_BYTES = 48000 * BYTES_PER_SAMPLE * 120

# ── faster-whisper 설정 (§0 결정) ────────────────────────────────────────────
WHISPER_MODEL_SIZE = "large-v3"
WHISPER_DEVICE = "cuda"
WHISPER_COMPUTE = "float16"

# UE5 language 힌트 → whisper 언어 코드. 미지정/미매핑은 None(자동 감지).
LANG_MAP = {"KR": "ko", "KO": "ko", "EN": "en", "US": "en", "JP": "ja", "JA": "ja"}

_model: Optional[WhisperModel] = None
# faster-whisper WhisperModel 은 스레드 안전하지 않음 — 동시 스트림의 GPU 추론을 직렬화.
_transcribe_lock = asyncio.Lock()


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _model
    logger.info(
        f"[ASR] faster-whisper 로드 시작 — {WHISPER_MODEL_SIZE} "
        f"({WHISPER_DEVICE}/{WHISPER_COMPUTE}) … (최초엔 모델 다운로드로 수십초 소요)"
    )
    t0 = time.perf_counter()
    try:
        _model = WhisperModel(WHISPER_MODEL_SIZE, device=WHISPER_DEVICE, compute_type=WHISPER_COMPUTE)
        logger.info(f"[ASR] 모델 로드 완료 ({(time.perf_counter()-t0):.1f}s)")
    except Exception as e:  # OOM/CUDA 미설치/다운로드 실패 — 서비스 기동은 유지, health 가 loading 반영
        logger.error(f"[ASR] 모델 로드 치명적 실패: {e}")
        _model = None
    # 프리워밍 — 0.5s 무음으로 첫 호출 cold-start 지연 제거 (모델 로드 성공 시에만)
    if _model is not None:
        try:
            warm = np.zeros(TARGET_SAMPLE_RATE // 2, dtype=np.float32)
            segs, _ = _model.transcribe(warm, language="ko")
            list(segs)
            logger.info("[ASR] 프리워밍 완료")
        except Exception as e:  # noqa: BLE001
            logger.warning(f"[ASR] 프리워밍 실패(무시): {e}")
    yield
    _model = None


app = FastAPI(title="ASRService", version="0.2.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://127.0.0.1:8000", "http://localhost:8000"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
async def health() -> dict:
    return {
        "status": "ok" if _model is not None else "loading",
        "service": "asr-faster-whisper",
        "model": WHISPER_MODEL_SIZE,
        "device": WHISPER_DEVICE,
        "version": "0.2.0",
    }


def _transcribe(pcm_bytes: bytes, sample_rate: int, language: Optional[str]) -> str:
    """동기 인식 — asyncio.to_thread 로 호출(이벤트 루프 비차단)."""
    if _model is None or len(pcm_bytes) < BYTES_PER_SAMPLE * 2:
        return ""

    # 홀수 바이트 → np.frombuffer ValueError 크래시 방지: 샘플 크기 배수로 절단.
    rem = len(pcm_bytes) % BYTES_PER_SAMPLE
    if rem:
        pcm_bytes = pcm_bytes[:-rem]

    # s16le → float32 [-1,1]
    audio = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32) / 32768.0
    # 네이티브 레이트 → 16kHz (whisper 규격)
    if sample_rate != TARGET_SAMPLE_RATE:
        audio = soxr.resample(audio, sample_rate, TARGET_SAMPLE_RATE)

    lang = LANG_MAP.get((language or "").upper())
    segments, _info = _model.transcribe(
        audio,
        language=lang,
        vad_filter=True,           # 무음/잡음 구간 제거
        beam_size=5,
        condition_on_previous_text=False,
    )
    return "".join(seg.text for seg in segments).strip()


@app.websocket("/ws/asr/stream")
async def ws_stream(websocket: WebSocket) -> None:
    await websocket.accept()
    request_id = f"asr_{uuid.uuid4().hex[:12]}"
    target_npc_id: Optional[str] = None
    player_id: str = "Player_1"
    sample_rate: int = TARGET_SAMPLE_RATE
    language: Optional[str] = None

    started_at: Optional[float] = None
    audio_buf = bytearray()
    chunk_count = 0

    try:
        while True:
            msg = await websocket.receive()
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
                    if sample_rate <= 0:   # 0/음수 → ZeroDivision·resample 크래시 방지
                        sample_rate = TARGET_SAMPLE_RATE
                    language = data.get("language")
                    started_at = time.perf_counter()
                    audio_buf = bytearray()
                    chunk_count = 0
                    logger.info(
                        f"[ASR] start request_id={request_id} target={target_npc_id} "
                        f"sr={sample_rate} lang={language}"
                    )
                    await websocket.send_json({"type": "ready", "request_id": request_id})

                elif mtype == "end":
                    total_bytes = len(audio_buf)
                    duration_audio_ms = int(total_bytes / (sample_rate * BYTES_PER_SAMPLE) * 1000)
                    t_rec = time.perf_counter()
                    async with _transcribe_lock:
                        transcript = await asyncio.to_thread(
                            _transcribe, bytes(audio_buf), sample_rate, language
                        )
                    infer_ms = int((time.perf_counter() - t_rec) * 1000)
                    logger.info(
                        f"[ASR] end request_id={request_id} chunks={chunk_count} "
                        f"audio_ms={duration_audio_ms} infer_ms={infer_ms} "
                        f"transcript=\"{transcript}\""
                    )
                    await websocket.send_json({
                        "type": "final",
                        "request_id": request_id,
                        "target_npc_id": target_npc_id,
                        "player_id": player_id,
                        "transcript": transcript,
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
                # 버퍼 상한 초과 시 중단 — end 미전송 무한 스트리밍 OOM/DoS 차단.
                if len(audio_buf) + len(msg["bytes"]) > MAX_AUDIO_BUF_BYTES:
                    logger.warning(f"[ASR] 버퍼 상한 초과 request_id={request_id} → 중단")
                    await websocket.send_json({
                        "type": "error", "request_id": request_id,
                        "code": "BUFFER_OVERFLOW", "message": "audio buffer size limit exceeded",
                    })
                    break
                audio_buf += msg["bytes"]
                chunk_count += 1

            elif msg.get("type") == "websocket.disconnect":
                logger.info(f"[ASR] 클라이언트 연결 종료 request_id={request_id}")
                break

    except WebSocketDisconnect:
        logger.info(f"[ASR] WebSocketDisconnect request_id={request_id}")
    except Exception as e:  # noqa: BLE001
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
# 보조 REST — 오디오 없이 임의 transcript 주입 (디버그·폴백 통로)
# ─────────────────────────────────────────────────────────────────────────────
class StubTranscribeRequest(BaseModel):
    text: str
    target_npc_id: Optional[str] = None
    player_id: str = "Debug_Browser"


@app.post("/api/asr/stub_transcribe")
async def api_stub_transcribe(req: StubTranscribeRequest) -> dict:
    return {
        "type": "final",
        "request_id": f"asr_{uuid.uuid4().hex[:12]}",
        "target_npc_id": req.target_npc_id,
        "player_id": req.player_id,
        "transcript": req.text,
        "duration_ms": 0,
        "language": "KR",
    }
