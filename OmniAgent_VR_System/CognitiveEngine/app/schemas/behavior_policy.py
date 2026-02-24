"""
File: behavior_policy.py
Purpose: High-level behavioral policy schemas sent from Cognitive Engine to UE5 AI.
Matches the C++ struct FBehaviorPolicy defined in Network/BehaviorPolicy.h
"""
from pydantic import BaseModel, Field
from typing import Optional
import uuid
import time


class BehavioralTraits(BaseModel):
    """
    Behavioral personality traits for NPC decision-making by real-time SLM.
    All values are normalized to 0 - 100 range.
    """
    aggression: float = Field(default=50, ge=0, le=100, description="Aggressive")
    fear: float = Field(default=50, ge=0, le=100, description="Fear")
    bravery: float = Field(default=50, ge=0, le=100, description="Brave")
    
    sanity: float = Field(default=0, ge=0, le=100, description="Sanity") #add Noise
    
    class Config:
        json_schema_extra = {
            "example": {
                "aggression": 70,
                "fear": 30,
                "bravery": 80,
                "sanity": 70
            }
        }


class BehaviorPolicy(BaseModel):
    """
    Base policy that defines NPC behavior directives.
    This is sent to UE5's PolicyCacheComponent for AI decision-making.
    """
    trace_id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    policy_version: int = Field(default=0, description="Increments on major policy changes")
    issued_at: float = Field(default_factory=time.time, description="Server world time when issued")
    ttl: float = Field(default=30.0, description="Time-to-live in seconds")
    target_guid: str = Field(..., description="UUID of the target actor (player, NPC, or object)")
    base_seed: int = Field(default=0, description="Seed for deterministic random decisions")
    
    # Behavioral traits (nested model)
    traits: BehavioralTraits = Field(default_factory=BehavioralTraits)
    
    # Bitflags for policy directives
    # Bit 0 (0x1): Urgent - Abort current action
    # Bit 1 (0x2): AllowAttack - Can engage target
    # Bit 2 (0x4): MustFollow - Track target continuously
    # Bit 3 (0x8): StealthMode - Reduce visibility
    policy_flags: int = Field(default=0, description="Bitwise flags for policy directives")

    class Config:
        json_schema_extra = {
            "example": {
                "trace_id": "550e8400-e29b-41d4-a716-446655440000",
                "policy_version": 1,
                "issued_at": 12345.67,
                "ttl": 30.0,
                "target_guid": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
                "base_seed": 42,
                "traits": {
                    "aggression": 70,
                    "fear": 30,
                    "bravery": 80
                },
                "policy_flags": 2
            }
        }


class PatchPolicy(BaseModel):
    """
    Lightweight update to an existing policy.
    Only includes fields that need to change, extending the TTL of the base policy.
    """
    trace_id: str = Field(..., description="Must match the base policy's trace_id")
    policy_version: int = Field(..., description="Must be >= base policy version")
    issued_at: float = Field(default_factory=time.time)
    ttl: Optional[float] = Field(None, description="New TTL if specified")
    
    # Optional trait updates (partial update)
    traits: Optional[BehavioralTraits] = Field(None, description="Partial trait update")
    policy_flags: Optional[int] = Field(None, description="Updated flags if specified")

    class Config:
        json_schema_extra = {
            "example": {
                "trace_id": "550e8400-e29b-41d4-a716-446655440000",
                "policy_version": 2,
                "issued_at": 12350.0,
                "ttl": 45.0,
                "traits": {"aggression": 90},
                "policy_flags": 3
            }
        }
