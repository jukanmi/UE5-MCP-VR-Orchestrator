"""
File: game_state.py
Purpose: Vector3D — 좌표 Pydantic 모델.
Includes validation to prevent NaN/Infinity coordinates from crashing the engine.
"""

from pydantic import BaseModel, field_validator
import math


class Vector3D(BaseModel):
    x: float
    y: float
    z: float

    @field_validator("x", "y", "z")
    @classmethod
    def check_finite(cls, v: float) -> float:
        if not math.isfinite(v):
            raise ValueError("Coordinates must be finite numbers (no NaN or Infinity)")
        return v
