from pydantic import BaseModel, Field, field_validator, model_validator
from typing import List, Optional, Literal, Union

ActionType = Literal["Move", "Speak", "Attack", "Interact", "Emote"]

class GameAction(BaseModel):
    action_type: ActionType
    target_id: Optional[str] = None
    parameters: dict = Field(default_factory=dict)
    
    # Validation for Damage Clamping
    @model_validator(mode='after')
    def clamp_damage_values(self):
        if self.action_type == "Attack":
            params = self.parameters
            if "damage" in params:
                raw_damage = params["damage"]
                # Hard-coded rule: Max damage is 100.
                if isinstance(raw_damage, (int, float)) and raw_damage > 100:
                    print(f"WARNING: Clamping damage from {raw_damage} to 100")
                    self.parameters["damage"] = 100
        return self

class ActionBatch(BaseModel):
    agent_id: str
    actions: List[GameAction]
    reasoning: Optional[str] = None
