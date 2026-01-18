"""
File: state.py
Purpose: Defines the Shared State object (AgentState) for the LangGraph workflow.
Contains Chat History, VR Context (GesPrompt), and the final ActionBatch.
"""
from typing import TypedDict, Annotated, List, Optional, Union, Dict, Any
from langgraph.graph.message import add_messages
from ..schemas.game_state import GameState
from ..schemas.vr_context import GesPrompt
from ..schemas.actions import ActionBatch

class AgentState(TypedDict):
    # Chat history
    messages: Annotated[List[Any], add_messages]
    
    # Context data from UE5
    game_state: Optional[GameState]
    vr_context: Optional[GesPrompt]
    
    # Internal Reasoning State
    next: str
    current_speaker: str
    analysis: Optional[Dict[str, Any]]  # Result of GesPrompt analysis (Intent: "Open", Target: "Door")
    
    # Final Output to UE5
    action_batch: Optional[ActionBatch]
