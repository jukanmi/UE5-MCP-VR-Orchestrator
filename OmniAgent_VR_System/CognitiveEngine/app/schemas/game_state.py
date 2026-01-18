"""
File: game_state.py
Purpose: Defines Pydantic V2 models for World Information (Entity, Vector3D).
Includes validation to prevent NaN/Infinity coordinates from crashing the engine.
"""
from pydantic import BaseModel, Field, field_validator
from typing import List, Optional
import math

class Vector3D(BaseModel):
    x: float
    y: float
    z: float

    @field_validator('x', 'y', 'z')
    @classmethod
    def check_finite(cls, v: float) -> float:
        if not math.isfinite(v):
            raise ValueError("Coordinates must be finite numbers (no NaN or Infinity)")
        return v

class Entity(BaseModel):
    id: str
    type: str
    location: Vector3D
    rotation: Vector3D
    tags: List[str] = Field(default_factory=list)

class GameState(BaseModel):
    player_location: Vector3D
    nearby_entities: List[Entity]
    world_time: str
    weather: str
