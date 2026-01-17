from pydantic import BaseModel, Field
from typing import Optional, List
from .game_state import Vector3D

class GestureData(BaseModel):
    gesture_type: str  # e.g., "Point", "Grab", "Wave"
    hand: str  # "Left", "Right"
    confidence: float
    target_entity_id: Optional[str] = None
    direction: Optional[Vector3D] = None

class GesPrompt(BaseModel):
    """
    Combined structure for Voice + Gesture context.
    This is the primary input for the Interface (Mediator) Agent.
    """
    player_id: str
    voice_transcript: str
    gestures: List[GestureData] = Field(default_factory=list)
    timestamp: float
    
    # Context resolved by UE5 before sending, or raw for Python logic:
    looking_at_entity_id: Optional[str] = None
