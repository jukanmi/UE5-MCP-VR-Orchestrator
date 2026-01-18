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
# ... (Imports preserved)

# SYSTEM PROMPT (Verbatim)
RULES_SYSTEM_PROMPT = """Role: Rules Agent

You are the authority on game rules.

Responsibilities:
Validate all numeric parameters proposed by other agents.
Apply clamping based on predefined rule limits.
Reject or correct invalid actions.
Map 'Intent' to specific 'GameAction' types.

Constraints:
Do not generate narrative text.
Do not decide intent or dialogue.
Output structured data only.
"""

def rules_node(state: AgentState):
    """
    Rules Agent (Referee).
    Uses LLM to decide HOW to execute the Intent (Resolution),
    Then Pydantic Schema enforces the 'Safety' (Validation).
    """
    analysis = state.get("analysis", {})
    intent_data = analysis.get("intent")
    
    # 1. Check for Intent
    if not intent_data:
         return {"next": "End"}
         
    # Extract intent fields safely
    # (Assuming intent_data is Pydantic object, but dict check safe)
    if hasattr(intent_data, 'model_dump'):
        i_dict = intent_data.model_dump()
    elif isinstance(intent_data, dict):
        i_dict = intent_data
    else:
        # Fallback
        i_dict = {"action_type": "Unknown", "raw_query": str(intent_data)}

    # 2. LLM Resolution (Intent -> GameAction construction plan)
    # We ask LLM to fill the GameAction schema based on the Intent.
    llm = get_llm(temperature=0.0)
    structured_llm = llm.with_structured_output(GameAction)
    
    prompt = ChatPromptTemplate.from_messages([
        ("system", RULES_SYSTEM_PROMPT),
        ("human", f"""
        Incoming Intent: {json.dumps(i_dict)}
        
        World Constants:
        MAX_DAMAGE = 100
        
        Task: 
        Convert this Intent into a valid GameAction. 
        If it violates rules (e.g. Fly), return an action with action_type='Emote' and parameters={{'emotion': 'Confused'}} or similar, OR just try to map it and let Validation fail?
        
        Actually, if it's invalid, you should probably try to map it and let the system catch it, or map to 'Emote'.
        If the user wants to 'Attack' with 9999 damage, put 9999. My internal validator will clamp it.
        """)
    ])
    
    actions = []
    rejection = None
    
    try:
        chain = prompt | structured_llm
        game_action = chain.invoke({})
        
        # 3. Add to list (Pydantic validator runs HERE during instantiation/re-validation)
        # Note: structured_llm returns an object that is *already* validated by Pydantic if using .with_structured_output(PydanticModel)
        # But we want to ensure our *custom* logic (Clamping) runs.
        # Pydantic V2 validates on init.
        
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
