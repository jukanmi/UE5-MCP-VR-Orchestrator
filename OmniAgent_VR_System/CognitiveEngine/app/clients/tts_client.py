"""
File: clients/tts_client.py
Role: TTSService(M1 stub / M2 VibeVoice) 호출용 HTTP 클라이언트.

WHY:
  - 명세서 §5.3 / vibevoice-dev-spec §6 의 REST 합성 등록 단계만 책임진다.
  - 실제 오디오 스트림은 UE5가 응답에 담긴 ws_url 로 직접 연결해서 받는다
    (TTS 통합 계획서 §9: 별도 NpcAudioResponse 타입 결정).

USAGE:
    info = await tts_client.synthesize(text="...", voice_id="ko_guard_01")
    # info = {"request_id": "...", "ws_url": "ws://...", "sample_rate": 16000, "channels": 1}
"""
from __future__ import annotations

import logging
import os
from typing import Optional

import httpx

logger = logging.getLogger("tts_client")

TTS_BASE_URL = os.environ.get("TTS_BASE_URL", "http://127.0.0.1:8001")
TTS_TIMEOUT_SEC = float(os.environ.get("TTS_TIMEOUT_SEC", "3.0"))


class TTSError(RuntimeError):
    pass


async def synthesize(
    text: str,
    voice_id: str,
    emotion: str = "neutral",
    speaking_rate: float = 1.0,
    pitch: float = 0.0,
    sample_rate: int = 16000,
) -> dict:
    """TTS 서버에 합성 요청을 등록하고 ws_url 을 반환.

    실패 시 TTSError. 호출자는 try/except 후 fallback (자막만) 처리.
    """
    body = {
        "text": text,
        "voice_id": voice_id,
        "emotion": emotion,
        "speaking_rate": speaking_rate,
        "pitch": pitch,
        "sample_rate": sample_rate,
        "output_format": "pcm_s16le",
    }
    url = f"{TTS_BASE_URL}/v1/tts/synthesize"
    try:
        async with httpx.AsyncClient(timeout=TTS_TIMEOUT_SEC) as client:
            r = await client.post(url, json=body)
            r.raise_for_status()
            data = r.json()
    except httpx.HTTPError as e:
        logger.warning(f"[TTS] synthesize 실패: {e}")
        raise TTSError(str(e)) from e

    if "request_id" not in data or "ws_url" not in data:
        raise TTSError(f"TTS 응답 형식 오류: {data}")
    return data
