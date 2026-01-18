"""
File: intent.py
Purpose: Defines the 'Intent' schema for the Interface Agent.
Represents the user's parsed intention (Action Type + Target) without engine-specific parameters.
"""
from pydantic import BaseModel
from typing import Optional

class Intent(BaseModel):
    action_type: str  # e.g., "Attack", "Open", "Talk"
    target_reference: Optional[str] = None  # Resolved target ID (e.g., "Door_42", "Goblin_01")
    raw_query: Optional[str] = None  # Original text for debugging
    confidence: float = 1.0
