"""
File: OmniAgent_VR_System/TTSService/voice_resolver.py
Role: npc_id → VibeVoice voice 매핑 해석.

WHY:
  - TTS 통합 계획서 M2 결정: voice_map 을 Python YAML 로 관리.
  - tts_client 가 NPC 이름 그대로 voice_id 로 쓰면 모델이 알 수 없는 키를 받음 → 폴백/에러.
  - 본 모듈이 npc_id → 실제 모델 voice 이름으로 변환.

USAGE:
    from OmniAgent_VR_System.TTSService.voice_resolver import resolve_voice
    voice = resolve_voice("Skadi")  # "en-Alice_woman"
"""
from __future__ import annotations

import logging
from functools import lru_cache
from pathlib import Path
from typing import Optional

import yaml

logger = logging.getLogger(__name__)

_MAP_PATH = Path(__file__).with_name("voice_map.yaml")


@lru_cache(maxsize=1)
def _load_map() -> dict:
    """voice_map.yaml 을 한 번만 로드해 캐시. 운영 중 갱신은 프로세스 재시작 필요."""
    try:
        with _MAP_PATH.open("r", encoding="utf-8") as f:
            data = yaml.safe_load(f) or {}
        return data
    except FileNotFoundError:
        logger.warning(f"[voice_resolver] {_MAP_PATH} 없음 — 모든 NPC 가 default 폴백")
        return {}
    except Exception as e:
        logger.warning(f"[voice_resolver] {_MAP_PATH} 로드 실패: {e}")
        return {}


def resolve_voice(npc_id: Optional[str]) -> str:
    """npc_id → voice 이름. 매핑 없으면 default."""
    data = _load_map()
    default = data.get("default", "en-Alice_woman")
    if not npc_id:
        return default
    npcs = data.get("npcs", {}) or {}
    return npcs.get(npc_id, default)
