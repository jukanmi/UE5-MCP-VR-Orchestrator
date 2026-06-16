"""
File: OmniAgent_VR_System/TTSService/server.py
Role: CosyVoice2-0.5B 기반 로컬 TTS 서비스 (zero-shot 음색 복제).

WHY:
  - 구 MeloTTS+OpenVoice 2단계(base wav→tone-color 변환)는 reference 원본과 음색
    유사도 한계. CosyVoice2 zero-shot 은 reference WAV+전사(prompt_text)로 직접 합성
    → 한국어 네이티브, 유사도 우위.
  - Windows 는 pynini(텍스트 정규화) 휠 부재로 구동 불가 → 본 서비스는 WSL2(Linux)
    에서 구동. Windows 의 CognitiveEngine/UE5 는 127.0.0.1:8001 로 그대로 접속
    (WSL2 localhost 포워딩). 프로토콜·포트 불변.

PROTOCOL (구버전과 동일 — UE5/CognitiveEngine 무변경):
  - REST  POST /v1/tts/synthesize → {request_id, ws_url, sample_rate, channels}
  - WS    /ws/tts/stream/{request_id} → audio_chunk(pcm_s16le, 16kHz mono) 스트림

NOTES:
  - CosyVoice2 출력은 24kHz → _resample_to_target 으로 16kHz 변환 후 송신.
  - voice_id(NPC ref) → base_voices/<ref>.wav prompt + voice_map ref_texts[ref] prompt_text.
  - 모델/리포/체크포인트는 WSL 홈(~/) 에. 환경변수:
      COSYVOICE_REPO       (기본 ~/CosyVoice)
      COSYVOICE_MODEL_DIR  (기본 ~/models/CosyVoice2-0.5B)
      CV_USE_GPU           (기본 1)
      CV_FP16              (기본 0)
      CV_TEXT_FRONTEND     (기본 1)   합성 텍스트 정규화 on/off
      CV_AUTO_TRANSCRIBE   (기본 1)   기동 시 빈 ref_texts whisper 자동 전사
      CV_BASE_VOICES_DIR   (기본 TTSService/base_voices)
      CV_VOICES_DIR        (기본 TTSService/voices)  레거시 ref 폴백
"""

from __future__ import annotations

import asyncio
import base64
import io
import logging
import os
import re
import sys
import time
import uuid
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

import numpy as np
import soundfile as sf
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, Response
from pydantic import BaseModel

from .ref_transcriber import fill_missing_ref_texts
from .voice_resolver import (
    list_voices,
    normalize_emotion,
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
USE_GPU = os.getenv("CV_USE_GPU", "1") == "1"
DEVICE = "cuda" if USE_GPU else "cpu"
TARGET_SAMPLE_RATE = 16000
CHUNK_MS = 100
CHANNELS = 1
FP16 = os.getenv("CV_FP16", "0") == "1"
# CosyVoice2 text normalization(wetext) 적용 여부. 한국어는 정규화 대상이 적지만
# 기본 파이프라인(True) 유지. 합성 오류 시 0 으로 끌 수 있게 노출.
TEXT_FRONTEND = os.getenv("CV_TEXT_FRONTEND", "1") == "1"
# 서버 기동 시 base_voices WAV 의 빈 ref_texts 를 whisper 로 자동 전사·기록.
AUTO_TRANSCRIBE = os.getenv("CV_AUTO_TRANSCRIBE", "1") == "1"

BASE_DIR = Path(__file__).parent
# 사람이 큐레이션한 reference WAV 원본. CosyVoice2 prompt 로 사용.
BASE_VOICES_DIR = Path(os.getenv("CV_BASE_VOICES_DIR", str(BASE_DIR / "base_voices")))
VOICES_DIR = Path(os.getenv("CV_VOICES_DIR", str(BASE_DIR / "voices")))  # 레거시 ref 폴백 경로

COSYVOICE_REPO = Path(os.getenv("COSYVOICE_REPO", str(Path.home() / "CosyVoice")))
COSYVOICE_MODEL_DIR = Path(os.getenv("COSYVOICE_MODEL_DIR", str(Path.home() / "models" / "CosyVoice2-0.5B")))

DEFAULT_VOICE_ID = os.getenv("CV_DEFAULT_VOICE_ID", "Skadi")

# CosyVoice 리포 + Matcha-TTS 서브모듈을 import 경로에 추가(PYTHONPATH 미설정 대비).
for _p in (COSYVOICE_REPO, COSYVOICE_REPO / "third_party" / "Matcha-TTS"):
    sp = str(_p)
    if _p.exists() and sp not in sys.path:
        sys.path.insert(0, sp)


# ─────────────────────────────────────────────────────────────────────────────
# 상태
# ─────────────────────────────────────────────────────────────────────────────
class _ModelState:
    cosyvoice: object = None  # CosyVoice2
    sample_rate: int = 24000  # 모델 로드 후 실제값으로 갱신
    prompt_cache: dict[str, object] = {}  # ref_stem → 절대경로 str (CosyVoice frontend 가 내부에서 재로드)


_state = _ModelState()

# 동시 NPC 합성 직렬화 — 공유 모델 thread-safety(GPU 도 어차피 직렬).
# 문장 단위로 acquire/release 라 여러 NPC 스트림이 문장별로 공평하게 교차.
_synth_lock = asyncio.Lock()
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
    language: Optional[str] = None  # 호환 유지(미사용 — CosyVoice2 자동 다국어)
    trace_id: str = ""  # 발원 msg_id 상속 → request_id 로 재사용, 로그 체인 통일


class SynthesizeResponse(BaseModel):
    request_id: str
    ws_url: str
    sample_rate: int
    channels: int = CHANNELS


# ─────────────────────────────────────────────────────────────────────────────
# 모델 로드
# ─────────────────────────────────────────────────────────────────────────────
def _load_wav_soundfile(wav, target_sr):
    """CosyVoice load_wav 대체 — torchaudio.load(=torchaudio 2.11 torchcodec 강제) 회피.

    torchcodec 휠은 CUDA13/특정 ffmpeg 요구로 cu128 환경에서 미적재 → soundfile 로 직접
    로드. 원본과 동일 산출: mono [1,T] tensor @target_sr.
    """
    import torch
    import torchaudio

    audio, sr = sf.read(str(wav), dtype="float32")
    if audio.ndim > 1:  # 멀티채널 → mono
        audio = audio.mean(axis=1)
    t = torch.from_numpy(audio).unsqueeze(0)  # [1, T]
    if int(sr) != int(target_sr):
        t = torchaudio.transforms.Resample(orig_freq=int(sr), new_freq=int(target_sr))(t)
    return t


def _patch_load_wav() -> None:
    """frontend 가 import 바인딩한 load_wav 까지 교체(torchcodec 우회)."""
    import cosyvoice.cli.frontend as _fe
    import cosyvoice.utils.file_utils as _fu

    _fu.load_wav = _load_wav_soundfile
    _fe.load_wav = _load_wav_soundfile


def _load_model_sync() -> object:
    if _state.cosyvoice is not None:
        return _state.cosyvoice
    from cosyvoice.cli.cosyvoice import CosyVoice2

    _patch_load_wav()  # CosyVoice2 import(frontend 포함) 후 패치

    if not COSYVOICE_MODEL_DIR.exists():
        raise RuntimeError(
            f"CosyVoice2 모델 디렉터리 없음: {COSYVOICE_MODEL_DIR} "
            f"(huggingface FunAudioLLM/CosyVoice2-0.5B 다운로드 필요)"
        )
    logger.info(f"[TTS] CosyVoice2 로드 (model={COSYVOICE_MODEL_DIR} fp16={FP16})")
    t0 = time.perf_counter()
    model = CosyVoice2(str(COSYVOICE_MODEL_DIR), load_jit=False, load_trt=False, fp16=FP16)
    _state.cosyvoice = model
    _state.sample_rate = int(getattr(model, "sample_rate", 24000))
    logger.info(f"[TTS] CosyVoice2 로드 완료 ({(time.perf_counter() - t0) * 1000:.0f}ms) sr={_state.sample_rate}")
    return model


REF_EXTS = (".wav", ".mp3", ".flac", ".ogg", ".m4a")


def _find_reference_wav(ref: str) -> Optional[Path]:
    """base_voices/<ref>.{wav,mp3,...} 우선, 레거시 voices/ 폴백."""
    for d in (BASE_VOICES_DIR, VOICES_DIR):
        for ext in REF_EXTS:
            p = d / f"{ref}{ext}"
            if p.exists():
                return p
    return None


def _resolve_prompt_path(ref: str) -> str:
    """ref → base_voices/<ref>.wav 절대경로 검증·캐시.

    이 CosyVoice 버전의 inference_zero_shot/cross_lingual 은 prompt_wav 로 **파일 경로**
    를 받아 frontend 가 내부에서 16k(token/spk)·24k(feat) 로 재로드한다(텐서 아님).
    """
    cached = _state.prompt_cache.get(ref)
    if cached is not None:
        return cached
    ref_wav = _find_reference_wav(ref)
    if ref_wav is None:
        raise RuntimeError(f"reference WAV 없음: {ref} (base_voices/{ref}.wav)")
    path = str(ref_wav.resolve())
    _state.prompt_cache[ref] = path
    logger.info(f"[TTS] prompt 경로: {ref} ← {ref_wav.name}")
    return path


# ─────────────────────────────────────────────────────────────────────────────
# 합성
# ─────────────────────────────────────────────────────────────────────────────
def _synthesize_sync(text: str, ref: str, ref_text: Optional[str], speed: float = 1.0) -> tuple[np.ndarray, int]:
    """CosyVoice2 zero-shot 합성 → (mono float32, sample_rate=24000).

    ref_text 있으면 inference_zero_shot(전사 기반, 유사도↑), 없으면 inference_cross_lingual.
    """
    model = _load_model_sync()
    prompt_path = _resolve_prompt_path(ref)
    eff_speed = float(speed) if speed is not None else 1.0

    if ref_text:
        gen = model.inference_zero_shot(
            text, ref_text, prompt_path, stream=False, speed=eff_speed, text_frontend=TEXT_FRONTEND
        )
    else:
        gen = model.inference_cross_lingual(
            text, prompt_path, stream=False, speed=eff_speed, text_frontend=TEXT_FRONTEND
        )

    chunks = [d["tts_speech"].cpu().numpy().reshape(-1) for d in gen]
    if not chunks:
        return np.zeros(0, dtype=np.float32), _state.sample_rate
    audio = np.concatenate(chunks).astype(np.float32)
    return audio, _state.sample_rate


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


# 문장 분리 — 문장단위 스트리밍 합성용. 종결부호/개행 뒤 분리, 짧은 조각은 병합(마이크로 합성 방지).
_SENTENCE_MIN_LEN = 12  # 이보다 짧은 조각은 다음 문장과 합쳐 합성 호출 낭비 방지


def _split_sentences(text: str) -> list[str]:
    text = text.strip()
    if not text:
        return []
    raw = re.split(r"(?<=[.!?。…！？\n])\s*", text)
    parts = [p.strip() for p in raw if p and p.strip()]
    if not parts:
        return [text]
    merged: list[str] = []
    for p in parts:
        if merged and len(merged[-1]) < _SENTENCE_MIN_LEN:
            merged[-1] = f"{merged[-1]} {p}".strip()
        else:
            merged.append(p)
    return merged


# ─────────────────────────────────────────────────────────────────────────────
# Lifespan
# ─────────────────────────────────────────────────────────────────────────────
async def _bg_transcribe_refs() -> None:
    """빈 ref_texts whisper 전사(백그라운드). 완료 시 map 캐시 무효화로 즉시 반영."""
    try:
        res = await asyncio.to_thread(fill_missing_ref_texts)
        if res.get("filled"):
            from .voice_resolver import _load_map

            _load_map.cache_clear()
            logger.info(f"[TTS] ref_texts 자동 전사 기록: {list(res['filled'])}")
        if res.get("failed"):
            logger.warning(f"[TTS] 전사 실패: {res['failed']}")
    except Exception as e:
        logger.warning(f"[TTS] 자동 전사 단계 건너뜀: {e}")


@asynccontextmanager
async def lifespan(app: FastAPI):
    try:
        await asyncio.to_thread(_load_model_sync)
        # base_voices WAV 의 빈 ref_texts 자동 전사 — whisper 첫 다운로드(~3GB)가 부팅을
        # 막지 않게 백그라운드 task 로. 완료 시 map 캐시 무효화해 이후 요청에 반영.
        if AUTO_TRANSCRIBE:
            asyncio.create_task(_bg_transcribe_refs())
        # voice_map 의 unique ref prompt 사전 캐시
        cached: set[str] = set()
        for npc_id, emo, vmeta in list_voices():
            if vmeta.ref in cached:
                continue
            try:
                await asyncio.to_thread(_resolve_prompt_path, vmeta.ref)
                cached.add(vmeta.ref)
            except Exception as e:
                logger.warning(f"[TTS] prompt pre-load 실패 npc={npc_id} emo={emo} ref={vmeta.ref}: {e}")
        logger.info(f"[TTS] prompt pre-load 완료 — refs={sorted(cached)}")
        # pre-warm — 첫 합성 지연 흡수
        try:
            t0 = time.perf_counter()
            warm_meta = resolve_voice_meta(DEFAULT_VOICE_ID, "Neutral")
            await asyncio.to_thread(
                _synthesize_sync, "준비 완료.", warm_meta.ref, warm_meta.ref_text, warm_meta.speed or 1.0
            )
            logger.info(f"[TTS] pre-warm 완료 ({(time.perf_counter() - t0) * 1000:.0f}ms)")
        except Exception as e:
            logger.warning(f"[TTS] pre-warm 실패 (계속 진행): {e}")
    except Exception as e:
        logger.error(f"[TTS] 초기화 실패 — synthesize 호출 시 503: {e}")
    yield


app = FastAPI(title="TTSService-CosyVoice2", version="0.7.0", lifespan=lifespan)

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
    logger.info(
        f"[TTS][trace={req.trace_id or request_id}] 합성 등록 npc={req.voice_id} emo={req.emotion} text_len={len(req.text)}"
    )
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
        await websocket.send_json(
            {
                "type": "error",
                "request_id": request_id,
                "code": "NOT_FOUND",
                "message": "request_id 미존재/소비됨",
                "retryable": False,
            }
        )
        await websocket.close()
        return

    if _state.cosyvoice is None:
        await websocket.send_json(
            {
                "type": "error",
                "request_id": request_id,
                "code": "MODEL_NOT_LOADED",
                "message": "CosyVoice2 미로드",
                "retryable": True,
            }
        )
        await websocket.close()
        return

    emotion = normalize_emotion(req.emotion)
    meta = resolve_voice_meta(req.voice_id, emotion)
    ref = meta.ref or DEFAULT_VOICE_ID
    speed = meta.speed if meta.speed is not None else 1.0
    target_sr = req.sample_rate or TARGET_SAMPLE_RATE
    started_at = time.perf_counter()
    sentences = _split_sentences(req.text)
    bytes_per_chunk = (target_sr * CHUNK_MS // 1000) * 2

    # 합성(producer) ↔ 송신(consumer) 분리: 문장 N+1 을 문장 N 재생 중 미리 합성.
    # 첫 음(TTFA)은 첫 문장 합성만 기다림 → 전체 발화 길이와 무관.
    pcm_queue: asyncio.Queue = asyncio.Queue(maxsize=2)  # 백프레셔 — 합성 과도 선행 방지
    producer: Optional[asyncio.Task] = None
    first_synth_ms = 0.0

    async def _produce() -> None:
        nonlocal first_synth_ms
        try:
            for idx, sent in enumerate(sentences):
                ts = time.perf_counter()
                async with _synth_lock:  # 동시 NPC 발화 직렬화(공유 모델 정합성)
                    native, native_sr = await asyncio.to_thread(_synthesize_sync, sent, ref, meta.ref_text, speed)
                dt = (time.perf_counter() - ts) * 1000.0
                if idx == 0:
                    first_synth_ms = dt
                logger.info(
                    f"[TTS] synth[{idx + 1}/{len(sentences)}] {dt:.0f}ms len={len(sent)} "
                    f"npc={req.voice_id} emo={emotion} ref={ref} speed={speed} zs={bool(meta.ref_text)}"
                )
                pcm = _float_to_pcm_s16le(_resample_to_target(native, native_sr, target_sr))
                await pcm_queue.put(pcm)
        finally:
            await pcm_queue.put(None)  # 예외/취소 시에도 consumer 를 항상 해제

    try:
        producer = asyncio.create_task(_produce())
        sequence = 0
        leftover = b""
        while True:
            item = await pcm_queue.get()
            if item is None:
                break
            buf = leftover + item
            off = 0
            # 문장 경계 무시하고 연속 청크 송신 — 문장 사이 silence 방지(gapless).
            while len(buf) - off >= bytes_per_chunk:
                chunk = buf[off : off + bytes_per_chunk]
                off += bytes_per_chunk
                await websocket.send_json(
                    {
                        "type": "audio_chunk",
                        "request_id": request_id,
                        "sequence": sequence,
                        "sample_rate": target_sr,
                        "channels": CHANNELS,
                        "audio_base64": base64.b64encode(chunk).decode("ascii"),
                        "is_last": False,
                    }
                )
                sequence += 1
                await asyncio.sleep(CHUNK_MS / 1000.0)
            leftover = buf[off:]

        if leftover:  # 마지막 잔여 PCM
            await websocket.send_json(
                {
                    "type": "audio_chunk",
                    "request_id": request_id,
                    "sequence": sequence,
                    "sample_rate": target_sr,
                    "channels": CHANNELS,
                    "audio_base64": base64.b64encode(leftover).decode("ascii"),
                    "is_last": True,
                }
            )
            sequence += 1

        elapsed_ms = int((time.perf_counter() - started_at) * 1000)
        logger.info(
            f"[TTS] stream 완료 sentences={len(sentences)} chunks={sequence} "
            f"first_synth={first_synth_ms:.0f}ms total={elapsed_ms}ms text_len={len(req.text)}"
        )
        await websocket.send_json(
            {
                "type": "completed",
                "request_id": request_id,
                "total_duration_ms": elapsed_ms,
            }
        )
    except WebSocketDisconnect:
        return
    except Exception as e:
        logger.exception(f"[TTS] 합성 실패: {e}")
        try:
            await websocket.send_json(
                {
                    "type": "error",
                    "request_id": request_id,
                    "code": "MODEL_ERROR",
                    "message": str(e),
                    "retryable": False,
                }
            )
        except Exception:
            pass
    finally:
        if producer is not None and not producer.done():
            producer.cancel()
        if producer is not None:
            try:
                await producer
            except (asyncio.CancelledError, Exception):
                pass
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
    "Neutral",
    "Happy",
    "Sad",
    "Angry",
    "Fear",
    "Surprised",
    "Disgusted",
    "Tired",
    "Pain",
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

    try:
        data = yaml.safe_load(_VOICE_MAP_PATH.read_text(encoding="utf-8")) or {}
    except FileNotFoundError:
        data = {}
    return {
        "map": data,
        "refs_on_disk": _refs_on_disk(),
        "cached_targets": sorted(_state.prompt_cache.keys()),  # 캐시된 prompt ref
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
    if _state.cosyvoice is None:
        raise HTTPException(status_code=503, detail="CosyVoice2 미로드")
    meta = resolve_voice_meta(body.npc_id, body.emotion)
    ref = meta.ref or DEFAULT_VOICE_ID
    speed = meta.speed if meta.speed is not None else 1.0
    try:
        async with _synth_lock:  # 공유 모델 정합성 — ws_stream producer 와 동일 직렬화
            audio, sr = await asyncio.to_thread(_synthesize_sync, body.text, ref, meta.ref_text, speed)
    except Exception as e:
        logger.exception(f"[TTS][debug] preview 합성 실패: {e}")
        raise HTTPException(status_code=500, detail="synthesis failed — see server log")
    buf = io.BytesIO()
    sf.write(buf, audio, sr, format="WAV", subtype="PCM_16")
    return Response(
        content=buf.getvalue(),
        media_type="audio/wav",
        headers={
            "X-Voice-Ref": ref,
            "X-Voice-Lang": (meta.lang or "auto"),
            "X-Voice-Speed": str(speed),
        },
    )


@app.post("/api/reload_voices")
async def api_reload_voices() -> dict:
    """voice_map 재로드 + prompt 캐시 재구축(reference WAV 교체 반영)."""
    from .voice_resolver import _load_map

    _load_map.cache_clear()
    _state.prompt_cache.clear()
    cached: list[str] = []
    failed: list[dict] = []
    for npc_id, emo, vmeta in list_voices():
        if vmeta.ref in cached:
            continue
        try:
            await asyncio.to_thread(_resolve_prompt_path, vmeta.ref)
            cached.append(vmeta.ref)
        except Exception as e:
            failed.append({"ref": vmeta.ref, "npc": npc_id, "emotion": emo, "error": str(e)})
    return {"extracted": cached, "failed": failed}


class TranscribeBody(BaseModel):
    force: bool = False  # True 면 기존 전사도 재전사(덮어쓰기). 기본은 빈칸만.


@app.post("/api/transcribe_refs")
async def api_transcribe_refs(body: TranscribeBody = TranscribeBody()) -> dict:
    """base_voices WAV 의 빈 ref_texts 를 whisper 전사로 채움(force=True 면 전체 재전사).

    기록 후 voice_map 캐시 + prompt 캐시 재구축해 즉시 반영(서버 재기동 불필요).
    """
    res = await asyncio.to_thread(fill_missing_ref_texts, None, None, body.force)
    if res.get("filled") or body.force:
        from .voice_resolver import _load_map

        _load_map.cache_clear()
        _state.prompt_cache.clear()
        for npc_id, emo, vmeta in list_voices():
            try:
                await asyncio.to_thread(_resolve_prompt_path, vmeta.ref)
            except Exception as e:
                logger.warning(f"[TTS] transcribe 후 prompt 재로드 실패 ref={vmeta.ref}: {e}")
    return res


@app.get("/health")
async def health() -> dict:
    return {
        "status": "ok" if _state.cosyvoice is not None else "model_not_loaded",
        "service": "tts-cosyvoice2",
        "device": DEVICE,
        "sample_rate": _state.sample_rate,
        "cached_voices": list(_state.prompt_cache.keys()),
    }
