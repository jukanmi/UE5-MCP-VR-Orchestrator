"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: supervisor.py                                                         ║
║ Role: ORCHESTRATOR (Pipeline Conductor)                                    ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Route state between agents in the correct order. Handle errors,           ║
║   fallbacks, and ensure the pipeline completes successfully.                ║
║                                                                              ║
║ ORCHESTRATION FLOW:                                                          ║
║   Interface_Input → Dialogue → Interface_Output → Rules → END              ║
║                                                                              ║
║ ROUTING LOGIC (based on current_speaker):                                   ║
║   - "Interface_Input"  → route to Dialogue                                  ║
║   - "Dialogue"         → route to Interface_Output                          ║
║   - "Interface_Output" → route to Rules                                     ║
║   - "Rules"            → END (or fallback to Dialogue if rejected)          ║
║                                                                              ║
║ ERROR HANDLING:                                                              ║
║   - Empty natural_context → warn but continue                               ║
║   - Empty raw_response → inject fallback "(confused)"                       ║
║   - Empty ActionBatch → create minimal Dialogue action("...", Confused)     ║
║   - Rules rejection → loop back to Dialogue for explanation                 ║
║                                                                              ║
║ BACKWARD COMPATIBILITY:                                                      ║
║   - Supports legacy "Interface" speaker (routes to Dialogue)                ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
from typing import Literal
from .state import AgentState
from ..schemas.actions import ActionBatch, NPCAction


def supervisor_node(state: AgentState):
    """
    Supervisor/Orchestrator Node.
    
    Routes between agents based on current_speaker.
    Handles fallback and error recovery.
    
    Flow:
    1. From Interface_Input → route to Dialogue
    2. From Dialogue → route to Interface_Output
    3. From Interface_Output → route to Rules
    4. From Rules → End (or Fallback if rejected)
    """
    current_speaker = state.get("current_speaker", "")
    
    print(f"[Supervisor] Routing from: {current_speaker}")
    
    # 1. After Interface Input → Go to Dialogue
    if current_speaker == "Interface_Input":
        natural_context = state.get("natural_context", "")
        if not natural_context:
            print("[Supervisor] WARNING: Empty natural_context from Interface Input")
        return {
            "next": "Dialogue",
            "current_speaker": "Supervisor"
        }
    
    # 2. After Dialogue → Go to Interface Output
    if current_speaker == "Dialogue":
        raw_response = state.get("raw_response", "")
        if not raw_response:
            print("[Supervisor] WARNING: Empty raw_response from Dialogue")
            return {
                "raw_response": '"..." (confused)',
                "next": "Interface_Output",
                "current_speaker": "Supervisor"
            }
        return {
            "next": "Interface_Output",
            "current_speaker": "Supervisor"
        }
    
    # 3. After Interface Output → Go to Rules
    if current_speaker == "Interface_Output":
        action_batch = state.get("action_batch")
        if not action_batch or not action_batch.actions:
            print("[Supervisor] WARNING: Empty ActionBatch from Interface Output")
            npc_id = state.get("target_npc", "Elara")
            return {
                "action_batch": _create_fallback_batch(npc_id),
                "next": "Rules",
                "current_speaker": "Supervisor"
            }
        return {
            "next": "Rules",
            "current_speaker": "Supervisor"
        }
    
    # 4. After Rules → End or Fallback
    if current_speaker == "Rules":
        action_batch = state.get("action_batch")
        
        rejected = False
        if action_batch:
            if "REJECTED" in (action_batch.reasoning or ""):
                rejected = True
            elif not action_batch.actions:
                rejected = True
        else:
            rejected = True
        
        if rejected:
            print("[Supervisor] Action REJECTED by Rules, requesting fallback...")
            return {
                "next": "Dialogue",
                "current_speaker": "Supervisor_Fallback",
                "natural_context": "System: Your previous action was rejected by game rules. Respond with speech only.",
            }
        
        print("[Supervisor] Pipeline complete!")
        return {"next": "End"}
    
    # Legacy: From old Interface → route to Dialogue (backward compatibility)
    if current_speaker == "Interface":
        return {
            "next": "Dialogue",
            "current_speaker": "Supervisor"
        }
    
    # Default
    print(f"[Supervisor] Unknown speaker: {current_speaker}, ending pipeline")
    return {"next": "End"}


def should_continue(state: AgentState) -> Literal[
    "Interface_Input", "Dialogue", "Interface_Output", "Rules", "End"
]:
    """Determine next node based on state."""
    return state.get("next", "End")


def _create_fallback_batch(npc_id: str) -> ActionBatch:
    """Create a minimal fallback ActionBatch."""
    return ActionBatch(
        agent_id=npc_id,
        actions=[NPCAction(
            action_category="Common",
            action_type="Dialogue",
            executor_npc_id=npc_id,
            emotion="Confused",
            parameters={"text": "..."},
        )],
        reasoning="Supervisor Fallback: Empty batch received"
    )
