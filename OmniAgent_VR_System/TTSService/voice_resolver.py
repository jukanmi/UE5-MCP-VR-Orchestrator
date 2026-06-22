"""
File: OmniAgent_VR_System/TTSService/voice_resolver.py
Role: (npc_id, emotion) → voice 메타 매핑 해석.

WHY:
  - voice_map.yaml 을 단일 진리원으로 관리.
  - NPC × emotion 조합을 ref / lang / speed 메타로 변환해 server.py 가 일관되게 사용.
  - 감정 enum 은 C++ EFacialState (interface_output.py::_normalize_emotion) 와 정합:
    Neutral | Happy | Sad | Angry | Fear | Surprised | Disgusted | Tired | Pain

USAGE:
    from .voice_resolver import resolve_voice_meta, list_voices
    meta = resolve_voice_meta("Skadi", "Happy")     # VoiceMeta(ref="Skadi_happy", lang="KR", speed=1.05)
    for npc_id, emotion, meta in list_voices(): ... # 부팅 pre-extract 용 (unique ref 만)
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Iterator, Optional

import yaml

logger = logging.getLogger(__name__)

_MAP_PATH = Path(__file__).with_name("voice_map.yaml")

# C++ EFacialState 와 정합. 입력은 대소문자 무시 후 PascalCase 로 정규화.
KNOWN_EMOTIONS = {
    "neutral": "Neutral",
    "happy": "Happy",
    "sad": "Sad",
    "angry": "Angry",
    "fear": "Fear",
    "surprised": "Surprised",
    "disgusted": "Disgusted",
    "tired": "Tired",
    "pain": "Pain",
}


def normalize_emotion(emotion: Optional[str]) -> str:
    if not emotion:
        return "Neutral"
    return KNOWN_EMOTIONS.get(emotion.strip().lower(), "Neutral")


@dataclass(frozen=True)
class VoiceMeta:
    ref: str
    lang: Optional[str] = None
    speed: Optional[float] = None
    # CosyVoice2 zero-shot prompt_text — reference WAV 의 전사. 최상위 ref_texts[ref] 에서 채움.
    # None 이면 server 가 inference_cross_lingual(전사 없는) 경로로 폴백.
    ref_text: Optional[str] = None


def _coerce_npc_entry(value, top_default: str) -> dict:
    """yaml npc 값을 표준 dict 로. 단축형(문자열) 도 처리."""
    if value is None:
        return {"default_ref": top_default}
    if isinstance(value, str):
        return {"default_ref": value}
    if isinstance(value, dict):
        return value
    logger.warning(f"[voice_resolver] 알 수 없는 NPC 값 형식: {value!r}")
    return {"default_ref": top_default}


def _coerce_emotion_entry(value) -> dict:
    """emotion entry → {ref?, speed?} dict."""
    if value is None:
        return {}
    if isinstance(value, str):
        return {"ref": value}
    if isinstance(value, dict):
        return value
    return {}


@lru_cache(maxsize=1)
def _load_map() -> dict:
    try:
        with _MAP_PATH.open("r", encoding="utf-8") as f:
            return yaml.safe_load(f) or {}
    except FileNotFoundError:
        logger.warning(f"[voice_resolver] {_MAP_PATH} 없음 — 모든 NPC default 폴백")
        return {}
    except Exception as e:
        logger.warning(f"[voice_resolver] {_MAP_PATH} 로드 실패: {e}")
        return {}


def _top_default() -> str:
    return str(_load_map().get("default") or "Skadi")


def resolve_voice_meta(npc_id: Optional[str], emotion: Optional[str] = None) -> VoiceMeta:
    """(npc_id, emotion) → VoiceMeta.

    Fallback:
      - npc 미매핑 → top default 만 채운 메타
      - emotion 미매핑 → NPC default_ref/speed 사용
    """
    data = _load_map()
    top_default = _top_default()
    if not npc_id:
        return VoiceMeta(ref=top_default, ref_text=_resolve_ref_text(data, top_default))

    npcs = data.get("npcs", {}) or {}
    if npc_id not in npcs:
        return VoiceMeta(ref=top_default, ref_text=_resolve_ref_text(data, top_default))

    npc = _coerce_npc_entry(npcs[npc_id], top_default=top_default)
    npc_ref = str(npc.get("default_ref") or top_default)
    npc_lang = npc.get("lang")
    npc_speed = npc.get("speed")

    emo_key = normalize_emotion(emotion)
    emotions = npc.get("emotions") or {}
    emo_entry = _coerce_emotion_entry(emotions.get(emo_key))

    ref = str(emo_entry.get("ref") or npc_ref)
    speed = emo_entry.get("speed")
    if speed is None:
        speed = npc_speed

    inline_ref_text = emo_entry.get("ref_text")
    if inline_ref_text is not None:
        resolved_ref_text = str(inline_ref_text).strip() or None
    else:
        resolved_ref_text = _resolve_ref_text(data, ref)

    return VoiceMeta(
        ref=ref,
        lang=str(npc_lang).upper() if npc_lang else None,
        speed=float(speed) if speed is not None else None,
        ref_text=resolved_ref_text,
    )


def _resolve_ref_text(data: dict, ref: str) -> Optional[str]:
    """최상위 ref_texts[ref] → CosyVoice2 zero-shot prompt_text. 없으면 None(cross_lingual 폴백)."""
    rt = (data.get("ref_texts") or {}).get(ref)
    if rt is None:
        return None
    rt = str(rt).strip()
    return rt or None


def resolve_voice(npc_id: Optional[str], emotion: Optional[str] = None) -> str:
    """하위 호환: ref(stem) 만 반환."""
    return resolve_voice_meta(npc_id, emotion).ref


def list_voices() -> Iterator[tuple[str, str, VoiceMeta]]:
    """부팅 pre-extract 용. (npc_id, emotion, meta) 를 unique ref 기준으로 산출.

    - 최상위 default ref 포함
    - 각 NPC 의 default_ref + 정의된 모든 emotion ref
    - 같은 ref 가 여러 (NPC, emotion) 에 걸치면 한 번만 yield
    """
    data = _load_map()
    top_default = _top_default()
    npcs = data.get("npcs", {}) or {}

    seen: set[str] = set()

    if top_default not in seen:
        yield ("__default__", "Neutral", VoiceMeta(ref=top_default))
        seen.add(top_default)

    for npc_id, raw in npcs.items():
        npc = _coerce_npc_entry(raw, top_default=top_default)
        npc_ref = str(npc.get("default_ref") or top_default)
        npc_lang = npc.get("lang")
        npc_speed = npc.get("speed")

        if npc_ref not in seen:
            yield (
                npc_id,
                "Neutral",
                VoiceMeta(
                    ref=npc_ref,
                    lang=str(npc_lang).upper() if npc_lang else None,
                    speed=float(npc_speed) if npc_speed is not None else None,
                ),
            )
            seen.add(npc_ref)

        for emo_key, emo_raw in (npc.get("emotions") or {}).items():
            entry = _coerce_emotion_entry(emo_raw)
            ref = str(entry.get("ref") or npc_ref)
            if ref in seen:
                continue
            speed = entry.get("speed")
            if speed is None:
                speed = npc_speed
            yield (
                npc_id,
                normalize_emotion(emo_key),
                VoiceMeta(
                    ref=ref,
                    lang=str(npc_lang).upper() if npc_lang else None,
                    speed=float(speed) if speed is not None else None,
                ),
            )
            seen.add(ref)
