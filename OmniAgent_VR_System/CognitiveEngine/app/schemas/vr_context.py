"""
File: vr_context.py
Purpose: Defines the 'GesPrompt' structure for Multimodal Input.
Combines Voice Transcript with Gesture Data to enable deictic resolution (interpreting "this/that").
"""

from pydantic import BaseModel, Field
from typing import Optional, List
from .envelope import PromptPayload
from .game_state import Vector3D


class GestureData(BaseModel):
    gesture_type: str  # e.g., "Point", "Grab", "Wave"
    hand: str  # "Left", "Right"
    confidence: float
    target_entity_id: Optional[str] = None
    direction: Optional[Vector3D] = None
    location: Optional[Vector3D] = None  # Hand location for "here/there" reference
    held_object_id: Optional[str] = None  # Context: What is the hand holding?


class NPCRelation(BaseModel):
    """
    Represents the affinity/reputation relationship between two entities.
    """

    source_id: str
    target_id: str
    affinity_score: int = 0  # Standard scale -100 to 100
    reputation_tag: str = "Neutral"  # Hostile, Neutral, Friendly
    last_interaction: Optional[str] = None
    is_dirty: bool = False  # Used internally by DB Manager to track changes


class GesPrompt(PromptPayload):
    """
    그래프 내부용 prompt 컨텍스트 — Envelope 의 PromptPayload 에 수신 시각을 더하고,
    제스처·위치를 dict 가 아닌 타입 모델로 승격한 것. 필드를 따로 열거하지 않는 이유:
    두 모델이 사실상 같은 필드 묶음이라 한쪽에 필드가 늘 때 다른 쪽을 빠뜨리는 사고를 막기 위함.
    """

    timestamp: float
    gestures: List[GestureData] = Field(default_factory=list)
    player_location: Optional[Vector3D] = None  # "이리 와" 류 명령의 기준 좌표
