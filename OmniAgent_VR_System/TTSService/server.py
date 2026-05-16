"""
File: OmniAgent_VR_System/TTSService/server.py
Role: VibeVoice-Realtime-0.5B 기반 로컬 TTS 서비스 (M2).

WHY:
  - M1 사인파 스텁을 실제 모델로 교체. 명세서 §5.3 + vibevoice-dev-spec §6 의 API 유지.
  - 같은 프로세스 안에서 VibeVoice 로드 — 별도 IPC 오버헤드 없음
    (TTS 통합 계획서 M2 결정: 같은 프로세스, 추후 소형 모델 마이그레이션 옵션 유지).
  - 첫 호출 cold-start 회피 위해 startup 에서 모델 로드 + dummy 합성으로 pre-warm.

PROTOCOL (M1 과 호환 유지 — UE5 측 변경 없음):
  - POST /v1/tts/synthesize
      body: { text, voice_id, emotion?, speaking_rate?, pitch?, sample_rate? }
      resp: { request_id, ws_url, sample_rate, channels }
  - WS  /ws/tts/stream/{request_id}
      push:
        { type:"audio_chunk", request_id, sequence, sample_rate, channels,
          audio_base64, is_last }
        ...
        { type:"completed", request_id, total_duration_ms }

NOTES:
  - 청크 포맷: pcm_s16le, 16kHz mono (TTS 통합 계획서 §9 결정).
  - VibeVoice 는 24kHz float 출력 → 16kHz int16 으로 리샘플 후 청크 분할.
  - M2 는 full-generate 후 100ms 청크 분할. 진짜 token streaming 은 M3 검토.

RUN (반드시 프로젝트 루트에서):
  python -m uvicorn OmniAgent_VR_System.TTSService.server:app --host 127.0.0.1 --port 8001
"""
from __future__ import annotations

import asyncio
import base64
import logging
import math
import os
import time
import uuid
from contextlib import asynccontextmanager
from typing import Optional

import numpy as np
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import BaseModel

from .voice_resolver import resolve_voice

logger = logging.getLogger("tts")
logger.setLevel(logging.INFO)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s | %(message)s",
    datefmt="%H:%M:%S",
)

# ─────────────────────────────────────────────────────────────────────────────
# Config
# ─────────────────────────────────────────────────────────────────────────────
MODEL_ID = os.getenv("VIBEVOICE_MODEL_ID", "microsoft/VibeVoice-Realtime-0.5B")
DEVICE = os.getenv("VIBEVOICE_DEVICE", "cuda")  # "cuda" 또는 "cpu"
DTYPE_NAME = os.getenv("VIBEVOICE_DTYPE", "float16")  # "float16" / "bfloat16" / "float32"
TARGET_SAMPLE_RATE = 16000  # 출력 청크 sample rate (M1 호환)
CHUNK_MS = 100
CHANNELS = 1
SAMPLES_PER_CHUNK = TARGET_SAMPLE_RATE * CHUNK_MS // 1000  # 1600


# ─────────────────────────────────────────────────────────────────────────────
# 모델 상태 — startup 에서 채움
# ─────────────────────────────────────────────────────────────────────────────
class _ModelState:
    model = None
    processor = None
    native_sample_rate: int = 24000  # 모델 로드 후 갱신


_state = _ModelState()
_pending: dict[str, "SynthesizeRequest"] = {}


# ─────────────────────────────────────────────────────────────────────────────
# Schemas
# ─────────────────────────────────────────────────────────────────────────────
class SynthesizeRequest(BaseModel):
    text: str
    voice_id: str = "default"  # NPC AgentID (voice_resolver 로 실제 모델 voice 변환)
    emotion: str = "neutral"   # M2 는 무시. M3 에서 매핑.
    speaking_rate: float = 1.0
    pitch: float = 0.0
    sample_rate: int = TARGET_SAMPLE_RATE
    output_format: str = "pcm_s16le"


class SynthesizeResponse(BaseModel):
    request_id: str
    ws_url: str
    sample_rate: int
    channels: int = CHANNELS


# ─────────────────────────────────────────────────────────────────────────────
# 모델 로드 + 인퍼런스 헬퍼
# ─────────────────────────────────────────────────────────────────────────────
def _load_model_sync() -> None:
    """transformers + torch import 는 lazy — 임포트 자체에 수 초 걸림."""
    import torch
    from transformers import AutoModel, AutoProcessor

    dtype_map = {
        "float16": torch.float16,
        "bfloat16": torch.bfloat16,
        "float32": torch.float32,
    }
    dtype = dtype_map.get(DTYPE_NAME, torch.float16)
    use_cuda = DEVICE.startswith("cuda") and torch.cuda.is_available()
    actual_device = DEVICE if use_cuda else "cpu"
    if DEVICE.startswith("cuda") and not use_cuda:
        logger.warning("[TTS] CUDA 요청했으나 사용 불가 — CPU 폴백 (인퍼런스 매우 느릴 수 있음)")

    logger.info(f"[TTS] 모델 로딩 시작: {MODEL_ID} (device={actual_device}, dtype={DTYPE_NAME})")
    t0 = time.perf_counter()
    _state.processor = AutoProcessor.from_pretrained(MODEL_ID, trust_remote_code=True)
    _state.model = AutoModel.from_pretrained(
        MODEL_ID,
        trust_remote_code=True,
        torch_dtype=dtype if use_cuda else torch.float32,
    ).to(actual_device).eval()
    # 모델 native sample rate — 일부 모델은 config 에 존재
    sr = getattr(_state.model.config, "sample_rate", None) \
        or getattr(_state.processor, "sampling_rate", None) \
        or 24000
    _state.native_sample_rate = int(sr)
    dt = time.perf_counter() - t0
    logger.info(f"[TTS] 모델 로드 완료 ({dt:.1f}s), native_sr={_state.native_sample_rate}")


def _generate_audio_sync(text: str, voice: str) -> np.ndarray:
    """VibeVoice 호출 → float32 mono numpy array (native sample rate).

    실제 호출 시그니처는 모델별로 다르므로 trust_remote_code 가 노출한 인터페이스에 의존.
    표준 transformers TTS 패턴: processor 로 입력 준비 → model.generate.
    """
    import torch

    proc = _state.processor
    model = _state.model

    # VibeVoice processor 는 text + voice 를 받음 (모델별 시그니처 차이 있음)
    try:
        inputs = proc(text=text, voice_preset=voice, return_tensors="pt")
    except TypeError:
        # 일부 버전은 키워드가 다름 — 폴백
        inputs = proc(text=text, voice=voice, return_tensors="pt")
    inputs = {k: (v.to(model.device) if hasattr(v, "to") else v) for k, v in inputs.items()}

    with torch.inference_mode():
        outputs = model.generate(**inputs)

    # generate 반환 형식이 모델별로 다름 — audio_values / audios / 텐서 직접
    audio = None
    for attr in ("audio_values", "audios", "waveform", "audio"):
        if hasattr(outputs, attr):
            audio = getattr(outputs, attr)
            break
    if audio is None and isinstance(outputs, torch.Tensor):
        audio = outputs

    if audio is None:
        raise RuntimeError(f"VibeVoice generate 출력에서 audio 텐서를 찾지 못함 (type={type(outputs)})")

    # (B, T) 또는 (T,) → float32 mono numpy
    audio_t = audio.detach().to("cpu", dtype=torch.float32).squeeze().numpy()
    if audio_t.ndim > 1:
        audio_t = audio_t[0]
    return audio_t


def _resample_to_target(audio: np.ndarray, src_sr: int, dst_sr: int) -> np.ndarray:
    """단순 선형 보간 리샘플. soundfile 만으로는 리샘플 불가 → numpy 로 처리."""
    if src_sr == dst_sr:
        return audio
    ratio = dst_sr / src_sr
    new_len = int(round(len(audio) * ratio))
    if new_len <= 0:
        return np.zeros(0, dtype=np.float32)
    x_old = np.linspace(0, 1, num=len(audio), endpoint=False, dtype=np.float64)
    x_new = np.linspace(0, 1, num=new_len, endpoint=False, dtype=np.float64)
    return np.interp(x_new, x_old, audio).astype(np.float32)


def _float_to_pcm_s16le(audio: np.ndarray) -> bytes:
    """[-1.0, 1.0] float32 → int16 little-endian bytes."""
    clipped = np.clip(audio, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype(np.int16)
    return pcm.tobytes()


# ─────────────────────────────────────────────────────────────────────────────
# Lifespan — 모델 pre-load + warm
# ─────────────────────────────────────────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    try:
        await asyncio.to_thread(_load_model_sync)
        # Pre-warm: dummy 합성 1회로 GPU kernel 초기화. 첫 클라이언트 호출의 cold-start 흡수.
        try:
            t0 = time.perf_counter()
            await asyncio.to_thread(_generate_audio_sync, "warm up.", resolve_voice(None))
            logger.info(f"[TTS] pre-warm 완료 ({(time.perf_counter()-t0)*1000:.0f}ms)")
        except Exception as e:
            logger.warning(f"[TTS] pre-warm 실패 (계속 진행): {e}")
    except Exception as e:
        logger.error(f"[TTS] 모델 로드 실패 — synthesize 호출 시 503: {e}")
    yield


app = FastAPI(title="TTSService-VibeVoice", version="0.2.0", lifespan=lifespan)


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
            "type": "error", "request_id": request_id,
            "code": "NOT_FOUND",
            "message": "request_id 가 존재하지 않거나 이미 소비됨",
            "retryable": False,
        })
        await websocket.close()
        return

    if _state.model is None:
        await websocket.send_json({
            "type": "error", "request_id": request_id,
            "code": "MODEL_NOT_LOADED",
            "message": "VibeVoice 모델이 로드되지 않음 (서버 로그 확인)",
            "retryable": True,
        })
        await websocket.close()
        return

    voice = resolve_voice(req.voice_id)
    target_sr = req.sample_rate or TARGET_SAMPLE_RATE
    started_at = time.perf_counter()

    try:
        # 1) 합성 — GPU 호출은 별도 스레드 (asyncio 이벤트루프 비블로킹)
        t0 = time.perf_counter()
        audio_native = await asyncio.to_thread(_generate_audio_sync, req.text, voice)
        infer_ms = (time.perf_counter() - t0) * 1000.0
        logger.info(
            f"[TTS] generate {infer_ms:.0f}ms text_len={len(req.text)} "
            f"voice={voice} samples={len(audio_native)} sr={_state.native_sample_rate}"
        )

        # 2) target sr 로 리샘플 + int16 변환
        audio_16k = _resample_to_target(audio_native, _state.native_sample_rate, target_sr)
        pcm_bytes = _float_to_pcm_s16le(audio_16k)
        bytes_per_chunk = (target_sr * CHUNK_MS // 1000) * 2  # int16 = 2 bytes/sample
        total_chunks = max(1, math.ceil(len(pcm_bytes) / bytes_per_chunk))

        # 3) 청크 단위 push
        for sequence in range(total_chunks):
            start = sequence * bytes_per_chunk
            chunk = pcm_bytes[start : start + bytes_per_chunk]
            if not chunk:
                break
            is_last = sequence == total_chunks - 1
            await websocket.send_json({
                "type": "audio_chunk",
                "request_id": request_id,
                "sequence": sequence,
                "sample_rate": target_sr,
                "channels": CHANNELS,
                "audio_base64": base64.b64encode(chunk).decode("ascii"),
                "is_last": is_last,
            })
            # 청크 사이 양보 (CPU 부담 거의 없음, 클라이언트 재생 페이싱)
            await asyncio.sleep(CHUNK_MS / 1000.0)

        elapsed_ms = int((time.perf_counter() - started_at) * 1000)
        await websocket.send_json({
            "type": "completed",
            "request_id": request_id,
            "total_duration_ms": elapsed_ms,
        })
    except WebSocketDisconnect:
        return
    except Exception as e:
        logger.exception(f"[TTS] 합성 실패: {e}")
        try:
            await websocket.send_json({
                "type": "error", "request_id": request_id,
                "code": "MODEL_ERROR", "message": str(e), "retryable": False,
            })
        except Exception:
            pass
    finally:
        try:
            await websocket.close()
        except Exception:
            pass


@app.get("/health")
async def health() -> dict:
    return {
        "status": "ok" if _state.model is not None else "model_not_loaded",
        "service": "tts-vibevoice",
        "model": MODEL_ID,
        "native_sample_rate": _state.native_sample_rate,
    }
