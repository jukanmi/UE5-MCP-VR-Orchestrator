"""
File: rules.py
Purpose: Rules Agent (Referee).
1. Validates player actions against Game Rules using System Prompt.
2. Returns deterministic ActionBatch with strict schema validation.
3. Uses RejectResult for invalid actions.
"""
from ..state import AgentState
from ...schemas.actions import ActionBatch, GameAction, RejectResult
from ...schemas.intent import Intent
import json

from ...utils.llm_factory import get_llm
from langchain_core.prompts import ChatPromptTemplate

# SYSTEM PROMPT
RULES_SYSTEM_PROMPT = """Role: Rules Agent

You are the authority on game rules.

Responsibilities:
- Validate all numeric parameters proposed by other agents.
- Apply clamping based on predefined rule limits.
- Reject or correct invalid actions.
- Map 'Intent' to specific 'GameAction' types.

Valid GameAction Types:
- Move: Movement to a target location or entity
- Attack: Combat action against a target
- Interact: Object interaction (open, grab, use)
- Emote: Emotional expression
- Wait: Stop/pause

Constraints:
- Do not generate narrative text.
- Do not decide intent or dialogue.
- Output structured data only.
"""

def rules_node(state: AgentState):
    """
    Rules Agent (Referee).
    Uses LLM to decide HOW to execute the Intent (Resolution),
    Then Pydantic Schema enforces the 'Safety' (Validation).
    """
    analysis = state.get("analysis", {})
    intent_data = analysis.get("intent")
    vr_context = state.get("vr_context")
    
    # Get the NPC that should perform the action (who player is looking at)
    target_npc_id = "Unknown"
    if vr_context:
        if hasattr(vr_context, 'looking_at_entity_id'):
            target_npc_id = vr_context.looking_at_entity_id or "Unknown"
        elif isinstance(vr_context, dict):
            target_npc_id = vr_context.get("looking_at_entity_id", "Unknown")
    
    print(f"[Rules] Target NPC (looking_at): {target_npc_id}")
    
    # 1. Check for Intent
    if not intent_data:
         return {"next": "End"}
         
    # Extract intent fields safely
    if hasattr(intent_data, 'model_dump'):
        i_dict = intent_data.model_dump()
    elif isinstance(intent_data, dict):
        i_dict = intent_data
    else:
        i_dict = {"action_type": "Unknown", "raw_query": str(intent_data)}

    action_type = i_dict.get("action_type", "Unknown")

    # --- Fast Path for Move with Location (Skip LLM) ---
    if action_type == "Move" and i_dict.get("target_location"):
        target_loc = i_dict["target_location"]
        target_ref = i_dict.get("target_reference", "Player_1")
        
        print(f"[Rules] Fast Path: Move to {target_ref} at {target_loc}")
        
        game_action = GameAction(
            action_type="Move",
            target_id=target_ref,
            parameters={
                "x": target_loc.get("x", 0),
                "y": target_loc.get("y", 0),
                "z": target_loc.get("z", 0)
            }
        )
        
        # Use target_npc_id as agent_id so NPCManager knows which NPC to command
        batch = ActionBatch(
            agent_id=target_npc_id,  # Changed from "RulesAgent" to actual NPC ID!
            actions=[game_action],
            reasoning=f"Move to {target_ref}"
        )
        
        return {
            "action_batch": batch,
            "current_speaker": "Rules",
            "next": "End"
        }
    # ---------------------------------------------------

    # 2. LLM Resolution (Intent -> GameAction construction plan)
    llm = get_llm(temperature=0.0)
    structured_llm = llm.with_structured_output(GameAction)
    
    # Serialize intent to string to avoid template variable issues
    intent_json = json.dumps(i_dict, ensure_ascii=False)
    
    prompt = ChatPromptTemplate.from_messages([
        ("system", RULES_SYSTEM_PROMPT),
        ("human", """
Incoming Intent: {intent_json}

Task: 
Convert this Intent into a valid GameAction.
- For Move intents: set action_type="Move" and include x, y, z in parameters if target_location is provided
- For Attack intents: set action_type="Attack" with damage values
- For Talk/Chat intents: set action_type="Emote" 
- If the action is impossible (e.g. Fly), set action_type="Emote" with appropriate parameters
- If damage values are excessive, include them anyway - internal validator will clamp.
""")
    ])
    
    actions = []
    rejection = None
    
    try:
        chain = prompt | structured_llm
        game_action = chain.invoke({"intent_json": intent_json})
        actions.append(game_action)
        
    except Exception as e:
        print(f"Rules LLM Error: {e}")
        rejection = RejectResult(reason=f"Rule Processing Failed: {str(e)}")

    # 4. Final Packaging
    if rejection:
         batch = ActionBatch(
            agent_id="RulesAgent",
            actions=[],
            reasoning=f"REJECTED: {rejection.reason}"
        )
    else:
        try:
            batch = ActionBatch(
                agent_id="RulesAgent",
                actions=actions,
                reasoning=f"Processed intent: {i_dict.get('action_type')}"
            )
        except Exception as e:
             batch = ActionBatch(
                agent_id="RulesAgent",
                actions=[],
                reasoning=f"REJECTED: Schema Validation Failed - {e}"
            )

    return {
        "action_batch": batch,
        "current_speaker": "Rules",
        "next": "End"
    }
