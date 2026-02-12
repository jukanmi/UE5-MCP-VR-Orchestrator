"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: state.py                                                              ║
║ Role: SHARED STATE DEFINITION (Data Contract)                              ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Define the shared state object that flows through the entire LangGraph    ║
║   pipeline. Acts as the "blackboard" for inter-agent communication.         ║
║                                                                              ║
║ STATE LIFECYCLE (Section 8 Orchestra):                                      ║
║   1. UE5 sends vr_context (GesPrompt)                                       ║
║   2. Interface Input adds natural_context                                   ║
║   3. Dialogue adds raw_response                                             ║
║   4. Interface Output adds action_batch                                     ║
║   5. Rules validates action_batch                                           ║
║   6. UE5 receives final action_batch                                        ║
║                                                                              ║
║ FIELD CATEGORIES:                                                            ║
║   • Input:  vr_context (from UE5)                                           ║
║   • Pipeline: natural_context, raw_response, target_npc                     ║
║   • Routing: next, current_speaker                                          ║
║   • Output: action_batch (to UE5)                                           ║
║   • Legacy: analysis (for backward compatibility)                           ║
║                                                                              ║
║ IMMUTABILITY:                                                                ║
║   Field names and types are stable. New agents may ADD fields but must      ║
║   never REMOVE or RENAME existing fields without migration plan.            ║
╚══════════════════════════════════════════════════════════════════════════════╝
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
    
    # Internal Routing State
    next: str
    current_speaker: str
    
    # Legacy (backward compatibility)
    analysis: Optional[Dict[str, Any]]
    
    # Section 8: Orchestra Pipeline State
    natural_context: Optional[str]   # Interface Input → Dialogue
    raw_response: Optional[str]      # Dialogue → Interface Output
    target_npc: Optional[str]        # Target NPC ID (flows through pipeline)
    
    # Final Output to UE5
    action_batch: Optional[ActionBatch]

