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
  - voice_id (NPC AgentID) → resolve_voice_meta() → ref/lang/speed 메타.
  - reference WAV 는 base_voices/<ref>.wav (사람 큐레이션 원본).
  - 없으면 첫 사용 시 MeloTTS KR 샘플 자동 생성 → base_voices/<ref>.wav (이후 재사용).
  - voices/ 는 _processed/ SE 캐시 및 레거시 reference 폴백 경로.
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
import threading
import time
import uuid
import zipfile
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

import numpy as np
import soundfile as sf
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, Response
from pydantic import BaseModel

from .voice_resolver import (
    VoiceMeta,
    list_voices,
    normalize_emotion,
    resolve_voice,
    resolve_voice_meta,
)

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
# 사람이 큐레이션한 reference WAV 원본. 없으면 첫 사용 시 MeloTTS KR 로 자동 생성.
BASE_VOICES_DIR = Path(os.getenv("OV_BASE_VOICES_DIR", str(BASE_DIR / "base_voices")))
CKPT_ZIP_URL = "https://myshell-public-repo-host.s3.amazonaws.com/openvoice/checkpoints_v2_0417.zip"

DEFAULT_VOICE_ID = os.getenv("OV_DEFAULT_VOICE_ID", "Skadi")

_HANGUL_RE = re.compile(r"[가-힯ᄀ-ᇿ㄰-㆏]")

# 언어 코드 매핑: MeloTTS / OpenVoice base SE 파일명
LANG_MAP = {
    "KR": {"melo_key": "KR", "base_se": "kr.pth"},
    "EN": {"melo_key": "EN-Default", "base_se": "en-newest.pth"},
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
    trace_id: str = ""   # 발원 msg_id 상속 → request_id 로 재사용, 로그 체인 통일


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
    """reference WAV 가 없으면 MeloTTS KR base 로 6초 샘플 자동 생성 → base_voices/."""
    BASE_VOICES_DIR.mkdir(parents=True, exist_ok=True)
    out = BASE_VOICES_DIR / f"{voice_id}.wav"
    if out.exists():
        return out
    model = _load_melo_sync("KR")
    speaker_id = _state.melo_speaker_ids["KR"]
    text = (
        "안녕하세요. 저는 새로운 음성 시스템의 기준 샘플입니다. "
        "이 목소리를 바탕으로 다양한 캐릭터의 음색이 만들어집니다."
    )
    logger.info(f"[TTS] reference WAV 자동 생성: {voice_id} (MeloTTS KR base) → {out}")
    model.tts_to_file(text, speaker_id, str(out), speed=1.0)
    return out


REF_EXTS = (".wav", ".mp3", ".flac", ".ogg", ".m4a")


def _find_reference_wav(voice_id: str) -> Optional[Path]:
    """base_voices/<voice_id>.{wav,mp3,flac,ogg,m4a} 우선, 레거시 voices/ 폴백.

    NOTE: mp3/m4a 디코딩은 librosa(audioread) 경유 → ffmpeg 설치 필요.
    """
    for d in (BASE_VOICES_DIR, VOICES_DIR):
        for ext in REF_EXTS:
            p = d / f"{voice_id}{ext}"
            if p.exists():
                return p
    return None


_se_extract_lock = threading.Lock()


def _load_target_se_sync(voice_id: str) -> object:
    cached = _state.target_se_cache.get(voice_id)
    if cached is not None:
        return cached
    # 동일 voice_id 동시 추출 시 torch.save 파일 손상/레이스 방지 — 추출·캐싱 직렬화.
    with _se_extract_lock:
        cached = _state.target_se_cache.get(voice_id)
        if cached is not None:
            return cached
        return _extract_target_se_sync(voice_id)


def _extract_target_se_sync(voice_id: str) -> object:
    import torch

    ref_wav = _find_reference_wav(voice_id)
    if ref_wav is None:
        ref_wav = _gen_reference_wav_from_melo_sync(voice_id)

    # 디스크 SE 캐시 — 매 부팅 재추출 방지. ref WAV 가 캐시보다 새 것일 때만 재추출.
    se_cache = VOICES_DIR / "_processed" / f"{voice_id}.se.pth"
    if se_cache.exists() and se_cache.stat().st_mtime >= ref_wav.stat().st_mtime:
        try:
            target_se = torch.load(str(se_cache), map_location=DEVICE)
            _state.target_se_cache[voice_id] = target_se
            logger.info(f"[TTS] target SE 캐시 로드(재사용): {voice_id} ← {se_cache.name}")
            return target_se
        except Exception as e:
            # 손상/불완전 저장된 캐시 → 삭제 후 아래에서 재추출(서비스 전체 실패 방지).
            logger.warning(f"[TTS] target SE 캐시 로드 실패(손상 가능성), 재추출 진행: {e}")
            try:
                se_cache.unlink()
            except OSError:
                pass

    from openvoice import se_extractor

    conv = _load_converter_sync()
    logger.info(f"[TTS] target SE 추출(신규/변경): {voice_id} ← {ref_wav.name}")
    t0 = time.perf_counter()
    target_se, _audio_name = se_extractor.get_se(
        str(ref_wav), conv, vad=True, target_dir=str(VOICES_DIR / "_processed")
    )
    se_cache.parent.mkdir(parents=True, exist_ok=True)
    torch.save(target_se, str(se_cache))
    _state.target_se_cache[voice_id] = target_se
    logger.info(f"[TTS] target SE 추출·디스크 캐시 완료 ({(time.perf_counter()-t0)*1000:.0f}ms)")
    return target_se


# ─────────────────────────────────────────────────────────────────────────────
# 합성
# ─────────────────────────────────────────────────────────────────────────────
def _synthesize_sync(
    text: str, voice_id: str, language: str, speed: Optional[float] = None
) -> tuple[np.ndarray, int]:
    melo = _load_melo_sync(language)
    speaker_id = _state.melo_speaker_ids[language]
    base_se = _load_base_se_sync(language)
    target_se = _load_target_se_sync(voice_id)
    conv = _load_converter_sync()

    eff_speed = SPEED if speed is None else float(speed)

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        src_path = f.name
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        out_path = f.name
    try:
        melo.tts_to_file(text, speaker_id, src_path, speed=eff_speed)
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
        # voice_map.npcs × emotion 의 모든 unique ref → target SE 사전 추출
        extracted: set[str] = set()
        for npc_id, emo, vmeta in list_voices():
            if vmeta.ref in extracted:
                continue
            try:
                await asyncio.to_thread(_load_target_se_sync, vmeta.ref)
                extracted.add(vmeta.ref)
            except Exception as e:
                logger.warning(
                    f"[TTS] target SE pre-extract 실패 npc={npc_id} emo={emo} ref={vmeta.ref}: {e}"
                )
        logger.info(f"[TTS] pre-extract 완료 — refs={sorted(extracted)}")
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

# CognitiveEngine(8000) debug 페이지에서 cross-origin 으로 /api/preview 호출 허용.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://127.0.0.1:8000", "http://localhost:8000"],
    allow_methods=["*"],
    allow_headers=["*"],
    expose_headers=["X-Voice-Ref", "X-Voice-Lang", "X-Voice-Speed"],
)


# ─────────────────────────────────────────────────────────────────────────────
# REST
# ─────────────────────────────────────────────────────────────────────────────
@app.post("/v1/tts/synthesize", response_model=SynthesizeResponse)
async def synthesize(req: SynthesizeRequest) -> SynthesizeResponse:
    # 발원 trace_id(msg_id) 가 있으면 request_id 로 상속 — 이후 모든 WS 로그가
    # 같은 id 를 찍어 LLM↔TTS↔UE5 가 [trace=...] 한 줄로 꿰진다. 없으면 신규 생성.
    request_id = f"tts_{req.trace_id}" if req.trace_id else f"tts_{uuid.uuid4().hex[:12]}"
    _pending[request_id] = req
    logger.info(f"[TTS][trace={req.trace_id or request_id}] 합성 등록 npc={req.voice_id} emo={req.emotion} text_len={len(req.text)}")
    return SynthesizeResponse(
        request_id=request_id,
        ws_url=f"ws://127.0.0.1:8001/ws/tts/stream/{request_id}",
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

    emotion = normalize_emotion(req.emotion)
    meta = resolve_voice_meta(req.voice_id, emotion)
    voice_id = meta.ref or DEFAULT_VOICE_ID
    # 우선순위: 요청 language > voice_map meta lang > 텍스트 자동감지
    language = req.language or meta.lang or _detect_language(req.text)
    target_sr = req.sample_rate or TARGET_SAMPLE_RATE
    started_at = time.perf_counter()

    try:
        t0 = time.perf_counter()
        audio_native, native_sr = await asyncio.to_thread(
            _synthesize_sync, req.text, voice_id, language, meta.speed
        )
        infer_ms = (time.perf_counter() - t0) * 1000.0
        logger.info(
            f"[TTS] synth {infer_ms:.0f}ms text_len={len(req.text)} "
            f"npc={req.voice_id} emo={emotion} ref={voice_id} lang={language} "
            f"speed={meta.speed} samples={len(audio_native)} sr={native_sr}"
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


# ─────────────────────────────────────────────────────────────────────────────
# Debug UI
# ─────────────────────────────────────────────────────────────────────────────
_DEBUG_HTML_PATH = BASE_DIR / "debug.html"
_VOICE_MAP_PATH = BASE_DIR / "voice_map.yaml"
KNOWN_EMOTION_LIST = [
    "Neutral", "Happy", "Sad", "Angry", "Fear",
    "Surprised", "Disgusted", "Tired", "Pain",
]


class VoiceMapBody(BaseModel):
    map: dict


class PreviewBody(BaseModel):
    text: str
    npc_id: str
    emotion: Optional[str] = "Neutral"


def _refs_on_disk() -> list[str]:
    seen: set[str] = set()
    for d in (BASE_VOICES_DIR, VOICES_DIR):
        if d.exists():
            for ext in REF_EXTS:
                for p in d.glob(f"*{ext}"):
                    seen.add(p.stem)
    return sorted(seen)


@app.get("/debug", response_class=HTMLResponse)
async def debug_page() -> HTMLResponse:
    if _DEBUG_HTML_PATH.exists():
        return HTMLResponse(_DEBUG_HTML_PATH.read_text(encoding="utf-8"))
    return HTMLResponse("<h1>debug.html missing</h1>", status_code=404)


@app.get("/api/voice_map")
async def api_get_voice_map() -> dict:
    import yaml
    from .voice_resolver import _load_map
    try:
        data = yaml.safe_load(_VOICE_MAP_PATH.read_text(encoding="utf-8")) or {}
    except FileNotFoundError:
        data = {}
    return {
        "map": data,
        "refs_on_disk": _refs_on_disk(),
        "cached_targets": sorted(_state.target_se_cache.keys()),
        "known_emotions": KNOWN_EMOTION_LIST,
        "base_voices_dir": str(BASE_VOICES_DIR),
    }


@app.post("/api/voice_map")
async def api_set_voice_map(body: VoiceMapBody) -> dict:
    import yaml
    from .voice_resolver import _load_map
    try:
        yaml_text = yaml.safe_dump(body.map, allow_unicode=True, sort_keys=False)
        _VOICE_MAP_PATH.write_text(yaml_text, encoding="utf-8")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"voice_map.yaml 저장 실패: {e}")
    _load_map.cache_clear()
    return {"ok": True}


@app.post("/api/preview")
async def api_preview(body: PreviewBody) -> Response:
    if _state.converter is None:
        raise HTTPException(status_code=503, detail="OpenVoice converter 미로드")
    meta = resolve_voice_meta(body.npc_id, body.emotion)
    voice_id = meta.ref or DEFAULT_VOICE_ID
    language = meta.lang or _detect_language(body.text)
    try:
        audio, sr = await asyncio.to_thread(
            _synthesize_sync, body.text, voice_id, language, meta.speed
        )
    except Exception as e:
        logger.exception(f"[TTS][debug] preview 합성 실패: {e}")
        raise HTTPException(status_code=500, detail=str(e))
    buf = io.BytesIO()
    sf.write(buf, audio, sr, format="WAV", subtype="PCM_16")
    return Response(
        content=buf.getvalue(),
        media_type="audio/wav",
        headers={
            "X-Voice-Ref": voice_id,
            "X-Voice-Lang": language,
            "X-Voice-Speed": str(meta.speed if meta.speed is not None else SPEED),
        },
    )


@app.post("/api/reload_voices")
async def api_reload_voices() -> dict:
    from .voice_resolver import _load_map
    _load_map.cache_clear()
    _state.target_se_cache.clear()
    extracted: list[str] = []
    failed: list[dict] = []
    for npc_id, emo, vmeta in list_voices():
        if vmeta.ref in extracted:
            continue
        try:
            await asyncio.to_thread(_load_target_se_sync, vmeta.ref)
            extracted.append(vmeta.ref)
        except Exception as e:
            failed.append({"ref": vmeta.ref, "npc": npc_id, "emotion": emo, "error": str(e)})
    return {"extracted": extracted, "failed": failed}


@app.get("/health")
async def health() -> dict:
    return {
        "status": "ok" if _state.converter is not None else "model_not_loaded",
        "service": "tts-openvoice",
        "device": DEVICE,
        "loaded_languages": list(_state.melo_models.keys()),
        "cached_voices": list(_state.target_se_cache.keys()),
    }
