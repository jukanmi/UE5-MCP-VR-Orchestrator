"""
File: supervisor.py
Purpose: Supervisor Agent / Main Orchestrator.
1. Implements System Prompt for high-level coordination.
2. Routes to 'Rules' or 'Dialogue' based on Interface Intent.
3. Handles Fallback: If Rules reject, commands Dialogue to explain why.
"""
from typing import Literal
from .state import AgentState
from ..schemas.intent import Intent
from ..schemas.actions import ActionBatch, SpeakAction, GameAction

SUPERVISOR_SYSTEM_PROMPT = """Role: Supervisor Agent

You coordinate the outputs of other agents.

You do not invent actions yourself unless required to resolve conflicts.

Responsibilities:

Receive proposed actions from Dialogue and Rules agents.

Resolve conflicts (e.g. mutually exclusive actions).

Ensure action order is logically consistent.

Produce a single ActionBatch for one agent_id.

Constraints:

Do not change numeric values unless instructed by Rules.

Do not add new action types.

Do not interpret or execute actions.

Output:

One validated ActionBatch object only.

No explanations, no commentary."""

def supervisor_node(state: AgentState):
    """
    Supervisor Node.
    Orchestrates the workflow.
    """
    current_speaker = state.get("current_speaker")
    analysis = state.get("analysis", {})
    intent_data = analysis.get("intent")
    
    # 1. Routing from Interface
    if current_speaker == "Interface":
        # Check Intent
        if not intent_data:
            return {"next": "End"}
        
        intent = intent_data # It's already an object hopefully, or dict if serialized
        # Ensure it's accessed correctly
        action_type = getattr(intent, "action_type", "Unknown")
        
        if action_type in ["Attack", "Interact", "Move"]:
            return {"next": "Rules", "current_speaker": "Supervisor"}
        else:
            return {"next": "Dialogue", "current_speaker": "Supervisor"}

    # 2. Handling Return from Rules (Fallback Logic)
    if current_speaker == "Rules":
        action_batch = state.get("action_batch")
        
        # Check for Rejection or Empty Batch
        # We assume Rules might set a flag or we check the batch content
        # For now, if batch claims "Rejected" in reasoning or is empty but intent was action
        rejected = False
        if action_batch:
            if "REJECTED" in (action_batch.reasoning or ""):
                rejected = True
            elif not action_batch.actions:
                rejected = True
        else:
            rejected = True
            
        if rejected:
            # Fallback: Ask Dialogue to explain
            # We enforce this by setting next to Dialogue, and providing context
            return {
                "next": "Dialogue", 
                "current_speaker": "Supervisor_Fallback",
                "messages": ["System: Action was rejected. Explain to user."]
            }
        
        return {"next": "End"} # Success

    # 3. Handling Return from Dialogue
    if current_speaker == "Dialogue":
        return {"next": "End"}
        
    # Default Entry (if called directly or handling other states)
    return {"next": "End"}

def should_continue(state: AgentState) -> Literal["Dialogue", "Rules", "End", "Supervisor"]:
    # We might need to loop back to Supervisor if we update the graph
    return state["next"]
