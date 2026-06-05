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

import asyncio
import logging
import os
from typing import Optional

import httpx

logger = logging.getLogger("tts_client")

TTS_BASE_URL = os.environ.get("TTS_BASE_URL", "http://127.0.0.1:8001")
TTS_TIMEOUT_SEC = float(os.environ.get("TTS_TIMEOUT_SEC", "3.0"))
# 단기 백오프 1회 재시도 (PDF 설계서 §6.2): timeout/HTTP500/연결오류 시 0.2s 후 1번만.
# VR TTFA<600ms 예산 보호를 위해 고정 0.2s, 재시도 1회로 제한.
TTS_RETRY_BACKOFF_SEC = 0.2
TTS_MAX_ATTEMPTS = 2


class TTSError(RuntimeError):
    pass


async def synthesize(
    text: str,
    voice_id: str,
    emotion: str = "neutral",
    speaking_rate: float = 1.0,
    pitch: float = 0.0,
    sample_rate: int = 16000,
    trace_id: str = "",
) -> dict:
    """TTS 서버에 합성 요청을 등록하고 ws_url 을 반환.

    trace_id: 발원 msg_id 를 그대로 상속 → 서버 request_id 로 재사용되어
              LLM↔TTS↔UE5 로그가 [trace=...] 로 한 줄로 꿰진다.
    실패 시(0.2s 백오프 1회 재시도 후에도) TTSError. 호출자는 자막 fallback.
    """
    body = {
        "text": text,
        "voice_id": voice_id,
        "emotion": emotion,
        "speaking_rate": speaking_rate,
        "pitch": pitch,
        "sample_rate": sample_rate,
        "output_format": "pcm_s16le",
        "trace_id": trace_id,
    }
    url = f"{TTS_BASE_URL}/v1/tts/synthesize"
    last_err: Optional[Exception] = None
    for attempt in range(1, TTS_MAX_ATTEMPTS + 1):
        try:
            async with httpx.AsyncClient(timeout=TTS_TIMEOUT_SEC) as client:
                r = await client.post(url, json=body)
                r.raise_for_status()
                data = r.json()
            if "request_id" not in data or "ws_url" not in data:
                raise TTSError(f"TTS 응답 형식 오류: {data}")
            return data
        except (httpx.HTTPError, TTSError, ValueError) as e:
            # ValueError: 서버가 비-JSON(HTML 에러페이지 등) 반환 시 r.json() 가 던짐 → 재시도 대상.
            last_err = e
            if attempt < TTS_MAX_ATTEMPTS:
                logger.warning(
                    f"[TTS][trace={trace_id}] synthesize 실패(attempt {attempt}/{TTS_MAX_ATTEMPTS}), "
                    f"{TTS_RETRY_BACKOFF_SEC}s 후 재시도: {e}"
                )
                await asyncio.sleep(TTS_RETRY_BACKOFF_SEC)
            else:
                logger.warning(f"[TTS][trace={trace_id}] synthesize 최종 실패: {e}")

    raise TTSError(str(last_err)) from last_err
