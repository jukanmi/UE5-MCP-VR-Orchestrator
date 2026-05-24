"""
File: OmniAgent_VR_System/TTSService/server.py
Role: OpenVoice v2 + MeloTTS base 기반 로컬 TTS 서비스 (M2 → M3 voice diversity).

WHY:
  - MeloTTS 단독은 한국어 native 발음 OK 이나 화자 다양성 ❌ (KR 1명, EN 5명).
  - OpenVoice v2 는 MeloTTS base 출력에 tone color converter 를 얹어
    6~10초 reference WAV 로 임의 화자 음색 복제. NPC 마다 고유 음성.
  - License: MIT (상업 OK).

PROTOCOL (M1 호환 — UE5 측 변경 없음).

NOTES:
  - 청크 포맷: pcm_s16le, 16kHz mono.
  - 흐름: MeloTTS(text→base wav) → ToneColorConverter(base se → target se 변환) → 결과 wav.
  - voice_id (NPC AgentID) → resolve_voice() → reference WAV 경로.
  - reference WAV 없으면 첫 기동 시 MeloTTS KR 샘플 자동 생성 → voices/<voice_id>.wav.
  - checkpoints_v2/ 가 없으면 첫 기동 시 HF 에서 자동 다운로드(~수백MB).
"""
from __future__ import annotations

import asyncio
import base64
import io
import logging
import math
import os
import re
import tempfile
import time
import uuid
import zipfile
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

import numpy as np
import soundfile as sf
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
USE_GPU = os.getenv("OV_USE_GPU", "1") == "1"
DEVICE = "cuda" if USE_GPU else "cpu"
TARGET_SAMPLE_RATE = 16000
CHUNK_MS = 100
CHANNELS = 1
SPEED = float(os.getenv("OV_SPEED", "1.0"))

BASE_DIR = Path(__file__).parent
CKPT_DIR = Path(os.getenv("OV_CKPT_DIR", str(BASE_DIR / "checkpoints_v2")))
VOICES_DIR = Path(os.getenv("OV_VOICES_DIR", str(BASE_DIR / "voices")))
CKPT_ZIP_URL = "https://myshell-public-repo-host.s3.amazonaws.com/openvoice/checkpoints_v2_0417.zip"

DEFAULT_VOICE_ID = os.getenv("OV_DEFAULT_VOICE_ID", "Skadi")

_HANGUL_RE = re.compile(r"[가-힯ᄀ-ᇿ㄰-㆏]")

# 언어 코드 매핑: MeloTTS / OpenVoice base SE 파일명
LANG_MAP = {
    "KR": {"melo_key": "KR", "base_se": "kr.pth"},
    "EN": {"melo_key": "EN-Default", "base_se": "en-default.pth"},
}


# ─────────────────────────────────────────────────────────────────────────────
# 상태
# ─────────────────────────────────────────────────────────────────────────────
class _ModelState:
    converter: object = None                 # ToneColorConverter
    melo_models: dict[str, object] = {}      # language → MeloTTS
    melo_speaker_ids: dict[str, int] = {}
    base_se: dict[str, object] = {}          # language → src SE tensor
    target_se_cache: dict[str, object] = {}  # voice_id → tgt SE tensor


_state = _ModelState()
_pending: dict[str, "SynthesizeRequest"] = {}


# ─────────────────────────────────────────────────────────────────────────────
# Schemas
# ─────────────────────────────────────────────────────────────────────────────
class SynthesizeRequest(BaseModel):
    text: str
    voice_id: str = "default"
    emotion: str = "neutral"
    speaking_rate: float = 1.0
    pitch: float = 0.0
    sample_rate: int = TARGET_SAMPLE_RATE
    output_format: str = "pcm_s16le"
    language: Optional[str] = None


class SynthesizeResponse(BaseModel):
    request_id: str
    ws_url: str
    sample_rate: int
    channels: int = CHANNELS


# ─────────────────────────────────────────────────────────────────────────────
# Checkpoint 다운로드
# ─────────────────────────────────────────────────────────────────────────────
def _ensure_silero_trust_sync() -> None:
    """torch.hub 가 snakers4/silero-vad 신뢰 확인 프롬프트를 띄움 → 비대화형 EOF.
    한 번 trust_repo=True 로 받아두면 캐시되어 이후 호출 정상."""
    try:
        import torch
        torch.hub.load(
            "snakers4/silero-vad",
            model="silero_vad",
            trust_repo=True,
            verbose=False,
        )
        logger.info("[TTS] silero-vad trust 캐시 완료")
    except Exception as e:
        logger.warning(f"[TTS] silero-vad trust 사전 호출 실패 (계속 진행): {e}")


def _ensure_checkpoints_sync() -> None:
    converter_dir = CKPT_DIR / "converter"
    if (converter_dir / "checkpoint.pth").exists() and (converter_dir / "config.json").exists():
        return
    import httpx

    CKPT_DIR.parent.mkdir(parents=True, exist_ok=True)
    logger.info(f"[TTS] checkpoints_v2 다운로드: {CKPT_ZIP_URL}")
    t0 = time.perf_counter()
    with httpx.stream("GET", CKPT_ZIP_URL, timeout=600.0, follow_redirects=True) as r:
        r.raise_for_status()
        buf = io.BytesIO()
        for chunk in r.iter_bytes(1024 * 1024):
            buf.write(chunk)
    buf.seek(0)
    logger.info(f"[TTS] zip 다운로드 완료 ({(time.perf_counter()-t0):.1f}s, {buf.getbuffer().nbytes/1e6:.0f}MB), 압축 해제 중...")
    with zipfile.ZipFile(buf) as zf:
        zf.extractall(CKPT_DIR.parent)
    logger.info(f"[TTS] checkpoints_v2 준비 완료 → {CKPT_DIR}")


# ─────────────────────────────────────────────────────────────────────────────
# 모델 로드
# ─────────────────────────────────────────────────────────────────────────────
def _detect_language(text: str) -> str:
    return "KR" if _HANGUL_RE.search(text) else "EN"


def _load_converter_sync() -> object:
    if _state.converter is not None:
        return _state.converter
    from openvoice.api import ToneColorConverter

    cfg = CKPT_DIR / "converter" / "config.json"
    ckpt = CKPT_DIR / "converter" / "checkpoint.pth"
    logger.info(f"[TTS] ToneColorConverter 로드 (device={DEVICE})")
    t0 = time.perf_counter()
    conv = ToneColorConverter(str(cfg), device=DEVICE)
    conv.load_ckpt(str(ckpt))
    _state.converter = conv
    logger.info(f"[TTS] converter 로드 완료 ({(time.perf_counter()-t0)*1000:.0f}ms)")
    return conv


def _load_melo_sync(language: str) -> object:
    cached = _state.melo_models.get(language)
    if cached is not None:
        return cached
    from melo.api import TTS

    info = LANG_MAP[language]
    logger.info(f"[TTS] MeloTTS base 로드: {language}")
    t0 = time.perf_counter()
    model = TTS(language=language, device=DEVICE)
    spk2id = model.hps.data.spk2id
    key = info["melo_key"] if info["melo_key"] in spk2id else next(iter(spk2id.keys()))
    _state.melo_models[language] = model
    _state.melo_speaker_ids[language] = spk2id[key]
    dt = time.perf_counter() - t0
    logger.info(
        f"[TTS] MeloTTS {language} 로드 완료 ({dt:.1f}s) "
        f"sr={model.hps.data.sampling_rate} speakers={list(spk2id.keys())} key='{key}'"
    )
    return model


def _load_base_se_sync(language: str) -> object:
    cached = _state.base_se.get(language)
    if cached is not None:
        return cached
    import torch

    se_path = CKPT_DIR / "base_speakers" / "ses" / LANG_MAP[language]["base_se"]
    se = torch.load(str(se_path), map_location=DEVICE)
    _state.base_se[language] = se
    return se


# ─────────────────────────────────────────────────────────────────────────────
# Target SE 추출 (NPC 별 reference WAV → embedding)
# ─────────────────────────────────────────────────────────────────────────────
def _gen_reference_wav_from_melo_sync(voice_id: str) -> Path:
    """reference WAV 가 없으면 MeloTTS KR base 로 6초 샘플 자동 생성."""
    VOICES_DIR.mkdir(parents=True, exist_ok=True)
    out = VOICES_DIR / f"{voice_id}.wav"
    if out.exists():
        return out
    model = _load_melo_sync("KR")
    speaker_id = _state.melo_speaker_ids["KR"]
    text = (
        "안녕하세요. 저는 새로운 음성 시스템의 기준 샘플입니다. "
        "이 목소리를 바탕으로 다양한 캐릭터의 음색이 만들어집니다."
    )
    logger.info(f"[TTS] reference WAV 자동 생성: {voice_id} (MeloTTS KR base)")
    model.tts_to_file(text, speaker_id, str(out), speed=1.0)
    return out


def _load_target_se_sync(voice_id: str) -> object:
    cached = _state.target_se_cache.get(voice_id)
    if cached is not None:
        return cached
    from openvoice import se_extractor

    ref_wav = VOICES_DIR / f"{voice_id}.wav"
    if not ref_wav.exists():
        ref_wav = _gen_reference_wav_from_melo_sync(voice_id)

    conv = _load_converter_sync()
    logger.info(f"[TTS] target SE 추출: {voice_id} ← {ref_wav.name}")
    t0 = time.perf_counter()
    target_se, _audio_name = se_extractor.get_se(
        str(ref_wav), conv, vad=True, target_dir=str(VOICES_DIR / "_processed")
    )
    _state.target_se_cache[voice_id] = target_se
    logger.info(f"[TTS] target SE 캐시 완료 ({(time.perf_counter()-t0)*1000:.0f}ms)")
    return target_se


# ─────────────────────────────────────────────────────────────────────────────
# 합성
# ─────────────────────────────────────────────────────────────────────────────
def _synthesize_sync(text: str, voice_id: str, language: str) -> tuple[np.ndarray, int]:
    melo = _load_melo_sync(language)
    speaker_id = _state.melo_speaker_ids[language]
    base_se = _load_base_se_sync(language)
    target_se = _load_target_se_sync(voice_id)
    conv = _load_converter_sync()

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        src_path = f.name
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        out_path = f.name
    try:
        melo.tts_to_file(text, speaker_id, src_path, speed=SPEED)
        conv.convert(
            audio_src_path=src_path,
            src_se=base_se,
            tgt_se=target_se,
            output_path=out_path,
            message="@MyShell",  # 빈 문자열은 watermark broadcast 에러 — 더미 토큰 필수
        )
        audio, sr = sf.read(out_path, dtype="float32")
    finally:
        for p in (src_path, out_path):
            try:
                os.unlink(p)
            except OSError:
                pass

    if audio.ndim > 1:
        audio = audio[:, 0]
    return audio.astype(np.float32), int(sr)


def _resample_to_target(audio: np.ndarray, src_sr: int, dst_sr: int) -> np.ndarray:
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
    clipped = np.clip(audio, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype(np.int16)
    return pcm.tobytes()


# ─────────────────────────────────────────────────────────────────────────────
# Lifespan
# ─────────────────────────────────────────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    try:
        await asyncio.to_thread(_ensure_checkpoints_sync)
        await asyncio.to_thread(_ensure_silero_trust_sync)
        await asyncio.to_thread(_load_converter_sync)
        await asyncio.to_thread(_load_melo_sync, "KR")
        await asyncio.to_thread(_load_melo_sync, "EN")
        await asyncio.to_thread(_load_base_se_sync, "KR")
        await asyncio.to_thread(_load_base_se_sync, "EN")
        # default voice (Skadi) reference + target SE 사전 추출
        await asyncio.to_thread(_load_target_se_sync, DEFAULT_VOICE_ID)
        try:
            t0 = time.perf_counter()
            await asyncio.to_thread(_synthesize_sync, "준비 완료.", DEFAULT_VOICE_ID, "KR")
            await asyncio.to_thread(_synthesize_sync, "Warm up.", DEFAULT_VOICE_ID, "EN")
            logger.info(f"[TTS] pre-warm 완료 ({(time.perf_counter()-t0)*1000:.0f}ms)")
        except Exception as e:
            logger.warning(f"[TTS] pre-warm 실패 (계속 진행): {e}")
    except Exception as e:
        logger.error(f"[TTS] 초기화 실패 — synthesize 호출 시 503: {e}")
    yield


app = FastAPI(title="TTSService-OpenVoice", version="0.6.0", lifespan=lifespan)


# ─────────────────────────────────────────────────────────────────────────────
# REST
# ─────────────────────────────────────────────────────────────────────────────
@app.post("/v1/tts/synthesize", response_model=SynthesizeResponse)
async def synthesize(req: SynthesizeRequest) -> SynthesizeResponse:
    request_id = f"tts_{uuid.uuid4().hex[:12]}"
    _pending[request_id] = req
    # ws_url 은 host 없는 경로만 반환한다. UE5(NPCAudioStreamComponent)가 수신 후
    # Config/DefaultGame.ini [OmniAgent] ServerHost+TTSPort 를 앞에 붙여 완전한
    # ws:// URL 로 만든다.
    # WHY: 서버는 자기 주소를 알 필요가 없다 — Quest 입장에서 127.0.0.1 은 자기
    #      자신이므로 서버가 host 를 advertise 할 수 없다. host 진실은 .ini 한 곳뿐.
    return SynthesizeResponse(
        request_id=request_id,
        ws_url=f"/ws/tts/stream/{request_id}",
        sample_rate=req.sample_rate,
    )


# ─────────────────────────────────────────────────────────────────────────────
# WebSocket
# ─────────────────────────────────────────────────────────────────────────────
@app.websocket("/ws/tts/stream/{request_id}")
async def ws_stream(websocket: WebSocket, request_id: str) -> None:
    await websocket.accept()
    req = _pending.pop(request_id, None)
    if req is None:
        await websocket.send_json({
            "type": "error", "request_id": request_id,
            "code": "NOT_FOUND",
            "message": "request_id 미존재/소비됨",
            "retryable": False,
        })
        await websocket.close()
        return

    if _state.converter is None:
        await websocket.send_json({
            "type": "error", "request_id": request_id,
            "code": "MODEL_NOT_LOADED",
            "message": "OpenVoice converter 미로드",
            "retryable": True,
        })
        await websocket.close()
        return

    voice_id = resolve_voice(req.voice_id) or DEFAULT_VOICE_ID
    language = req.language or _detect_language(req.text)
    target_sr = req.sample_rate or TARGET_SAMPLE_RATE
    started_at = time.perf_counter()

    try:
        t0 = time.perf_counter()
        audio_native, native_sr = await asyncio.to_thread(
            _synthesize_sync, req.text, voice_id, language
        )
        infer_ms = (time.perf_counter() - t0) * 1000.0
        logger.info(
            f"[TTS] synth {infer_ms:.0f}ms text_len={len(req.text)} "
            f"voice={voice_id} lang={language} samples={len(audio_native)} sr={native_sr}"
        )

        audio_16k = _resample_to_target(audio_native, native_sr, target_sr)
        pcm_bytes = _float_to_pcm_s16le(audio_16k)
        bytes_per_chunk = (target_sr * CHUNK_MS // 1000) * 2
        total_chunks = max(1, math.ceil(len(pcm_bytes) / bytes_per_chunk))

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
        "status": "ok" if _state.converter is not None else "model_not_loaded",
        "service": "tts-openvoice",
        "device": DEVICE,
        "loaded_languages": list(_state.melo_models.keys()),
        "cached_voices": list(_state.target_se_cache.keys()),
    }
