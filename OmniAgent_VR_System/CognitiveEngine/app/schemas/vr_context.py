"""
File: vr_context.py
Purpose: Defines the 'GesPrompt' structure for Multimodal Input.
Combines Voice Transcript with Gesture Data to enable deictic resolution (interpreting "this/that").
"""

from pydantic import BaseModel, Field
from typing import Optional, List, Dict, Any
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


class GesPrompt(BaseModel):
    """
    Combined structure for Voice + Gesture context.
    This is the primary input for the Interface (Mediator) Agent.
    """

    player_id: str
    voice_transcript: str
    gestures: List[GestureData] = Field(default_factory=list)
    timestamp: float
    last_event: Optional[str] = None  # e.g. "Hit", "Ambush"
    stats: Optional[Dict[str, float]] = None  # e.g. {"hp": 80, "agility": 0.9}

    player_location: Optional[Vector3D] = None  # Player's world location for "come here" commands

    # 대화 대상 NPC 인벤토리 — npc_id → [{id,name,desc,count,...}]. Stage1 컨텍스트 주입용.
    # 타입은 PromptPayload.npc_inventory 와 일치(일관성).
    npc_inventory: Optional[Dict[str, List[Dict[str, Any]]]] = None
