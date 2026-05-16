"""
File: schemas/npc_audio.py
Role: Python → UE5 방향의 NpcAudioResponse 스키마 (TTS 통합 계획서 §9 결정).

WHY:
  - 기존 ActionBatch/ModeActionRequest 와 생명주기를 분리하기 위해 별도 타입.
  - UE5 측 분기에서 "type" 필드만 보고 처리 가능하도록 최상위에 type 노출.

FIELDS:
  - type           : 고정 "npc_audio_response"
  - request_id     : TTS request_id (UE5 트레이싱용)
  - npc_id         : 발화 주체 NPC
  - dialogue_text  : 화면 자막 / 음성 실패 시 fallback
  - audio_stream   : { mode:"websocket", url, sample_rate, channels }
  - animation_metadata : { emotion, gesture?, look_at_player? }
"""
from __future__ import annotations

from typing import Literal, Optional
from pydantic import BaseModel, Field


class AudioStreamInfo(BaseModel):
    mode: Literal["websocket"] = "websocket"
    url: str
    sample_rate: int = 16000
    channels: int = 1


class AnimationMetadata(BaseModel):
    emotion: str = "neutral"
    gesture: Optional[str] = None
    look_at_player: bool = False


class NpcAudioResponse(BaseModel):
    type: Literal["npc_audio_response"] = "npc_audio_response"
    request_id: str
    npc_id: str
    dialogue_text: str
    audio_stream: AudioStreamInfo
    animation_metadata: AnimationMetadata = Field(default_factory=AnimationMetadata)
