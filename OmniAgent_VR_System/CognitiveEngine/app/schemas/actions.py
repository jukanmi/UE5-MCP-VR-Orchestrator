"""
File: actions.py
Purpose: Defines the 'ActionBatch' data contract for Engine Commands.
Includes strict validation logic (e.g., Clamping damage values) to enforce game rules at the schema level.
Also defines specific Action types like SpeakAction.
"""
from pydantic import BaseModel, Field, field_validator, model_validator
from typing import List, Optional, Literal, Union, Any
import json
import os

# Load Shared Data Constants
SHARED_DATA_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../SharedData/world_constants.json"))
try:
    with open(SHARED_DATA_PATH, "r") as f:
        WORLD_CONSTANTS = json.load(f)
except FileNotFoundError:
    print(f"WARNING: world_constants.json not found at {SHARED_DATA_PATH}. Using defaults.")
    WORLD_CONSTANTS = {"MAX_DAMAGE": 100}

class SpeakAction(BaseModel):
    action_type: Literal["Speak"] = "Speak"
    text: str
    emotion: str = "Neutral"
    target_listener: Optional[str] = None

class RejectResult(BaseModel):
    reason: str
    rejected: bool = True

class GameAction(BaseModel):
    action_type: Literal["Move", "Attack", "Interact", "Emote", "Speak"]
    target_id: Optional[str] = None
    parameters: dict = Field(default_factory=dict)
    
    # Validation for Damage Clamping
    @model_validator(mode='after')
    def clamp_damage_values(self):
        if self.action_type == "Attack":
            params = self.parameters
            if "damage" in params:
                raw_damage = params["damage"]
                max_dmg = WORLD_CONSTANTS.get("MAX_DAMAGE", 100)
                
                if isinstance(raw_damage, (int, float)) and raw_damage > max_dmg:
                    print(f"WARNING: Clamping damage from {raw_damage} to {max_dmg}")
                    self.parameters["damage"] = max_dmg
        return self

class ActionBatch(BaseModel):
    agent_id: str
    # Using Union to allow both SpeakAction (specific) and GameAction (generic)
    actions: List[Union[SpeakAction, GameAction]]
    reasoning: Optional[str] = None
