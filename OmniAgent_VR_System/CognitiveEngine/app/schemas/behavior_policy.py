from pydantic import BaseModel, Field, field_validator, ConfigDict
from typing import Optional, Literal
import uuid

class BehaviorPolicy(BaseModel):
    """
    Base behavioral policy schema for the 'BasePolicy' (Cerebrum/Long-term).
    """
    model_config = ConfigDict(extra="ignore")

    trace_id: str = Field(..., description="Unique trace ID for observability")
    policy_version: int = Field(..., description="Version to order updates (monotonic)")
    issued_at: float = Field(..., description="Server World Time (seconds) when issued")
    ttl: float = Field(..., description="Time To Live in seconds")
    target_guid: str = Field(..., description="UUID string of the target actor")
    base_seed: int = Field(..., description="Seed for deterministic RNG")
    
    # Traits (0.0 to 1.0)
    aggression: float = Field(..., description="0.0 to 1.0")
    fear: float = Field(..., description="0.0 to 1.0")
    vigilance: float = Field(..., description="0.0 to 1.0")
    
    policy_flags: int = Field(0, description="Bitmask for boolean flags (e.g. Urgent=1, AllowAttack=2)")

    @field_validator('target_guid')
    @classmethod
    def validate_guid(cls, v: str) -> str:
        try:
            uuid.UUID(v)
        except ValueError:
            raise ValueError("target_guid must be a valid UUID string")
        return v

    @field_validator('aggression', 'fear', 'vigilance', mode='before')
    @classmethod
    def clamp_traits(cls, v: float) -> float:
        if isinstance(v, (int, float)):
            return max(0.0, min(1.0, float(v)))
        return v


class PatchPolicy(BaseModel):
    """
    Patch policy for 'Cerebellum/Short-term' immediate overrides.
    Only non-None fields will overwrite the existing policy.
    """
    model_config = ConfigDict(extra="ignore")
    
    # Meta fields required for identification/application
    trace_id: str
    policy_version: int
    issued_at: float
    ttl: float
    target_guid: str
    
    # Optional overrideable fields
    base_seed: Optional[int] = None
    aggression: Optional[float] = None
    fear: Optional[float] = None
    vigilance: Optional[float] = None
    policy_flags: Optional[int] = None

    source: Literal["patch"] = "patch"

    @field_validator('target_guid')
    @classmethod
    def validate_guid(cls, v: str) -> str:
        try:
            uuid.UUID(v)
        except ValueError:
            raise ValueError("target_guid must be a valid UUID string")
        return v

    @field_validator('aggression', 'fear', 'vigilance', mode='before')
    @classmethod
    def clamp_traits(cls, v: Optional[float]) -> Optional[float]:
        if v is None:
            return None
        if isinstance(v, (int, float)):
            return max(0.0, min(1.0, float(v)))
        return v
