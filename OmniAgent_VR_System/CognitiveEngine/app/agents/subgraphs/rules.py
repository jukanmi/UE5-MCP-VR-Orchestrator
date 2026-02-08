import json
import subprocess
import re
from ..state import AgentState
from ...schemas.actions import ActionBatch, GameAction, WORLD_CONSTANTS

# SYSTEM PROMPT for Rules Validator
RULES_VALIDATOR_PROMPT_TEMPLATE = """Role: Game Rules Referee
You are an expert at game balance and safety.

Task:
Validate and correct the 'Proposed ActionBatch' based on the 'Original Intent'.

Rules:
1. Action Type Alignment: Ensure the action matches the player's intent.
2. Numeric Clamping (Global Constants):
   - Attack damage MUST NOT exceed {MAX_DAMAGE}.
   - Health adjustments MUST NOT exceed {MAX_HEALTH}.
   - Inventory operations MUST respect {MAX_INVENTORY_SLOTS} slots.
3. Logical Consistency: If intent is "Move to Door", target_id should be "Door".

Output ONLY valid JSON matching the ActionBatch schema:
{{"agent_id": "string", "actions": [{{"action_type": "string", "target_id": "string", "parameters": {{}}}}], "reasoning": "string"}}
"""

# Populate prompt with constants at module level
RULES_VALIDATOR_PROMPT = RULES_VALIDATOR_PROMPT_TEMPLATE.format(**WORLD_CONSTANTS)

def call_gemini_cli(prompt_text):
    """Calls Gemini CLI via subprocess to bypass API limits."""
    try:
        cmd = [
            "powershell", "-ExecutionPolicy", "Bypass", "-Command",
            f"gemini '{prompt_text}'"
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')
        if result.returncode != 0: return None
        output = result.stdout
        # Extract JSON from markdown
        json_match = re.search(r'```json\s*(.*?)\s*```', output, re.DOTALL)
        return json_match.group(1).strip() if json_match else output.strip()
    except: return None

def rules_node(state: AgentState):
    """
    Rules Agent (LLM-powered via CLI).
    Referees the proposed actions from Dialogue Agent.
    """
    batch = state.get("action_batch")
    analysis = state.get("analysis", {})
    intent_data = analysis.get("intent")
    
    if not batch:
        return {"next": "End", "current_speaker": "Rules"}

    # Prepare Context for LLM Referee
    intent_str = json.dumps(intent_data.model_dump() if hasattr(intent_data, 'model_dump') else intent_data, ensure_ascii=False)
    batch_str = json.dumps(batch.model_dump(), ensure_ascii=False)
    
    prompt = f"{RULES_VALIDATOR_PROMPT}\n\n[Original Intent]:\n{intent_str}\n\n[Proposed ActionBatch]:\n{batch_str}\n\nVerify and Correct the batch."

    # Call LLM 심판 (via CLI)
    cli_result = call_gemini_cli(prompt)
    
    if cli_result:
        try:
            data = json.loads(cli_result)
            # Ensure proper schema mapping
            new_batch = ActionBatch(**data)
            # Final safety check: ensure parameters are strings for C++
            for action in new_batch.actions:
                if action.parameters:
                    action.parameters = {str(k): str(v) for k, v in action.parameters.items()}
            
            print(f"[Rules] Corrected action via CLI. Reason: {new_batch.reasoning}")
            batch = new_batch
        except Exception as e:
            print(f"[Rules] CLI Parse Error, using original batch: {e}")
            batch.reasoning += " | Rules Notice: Verification failed, kept original."
    else:
        print("[Rules] CLI Unavailable, using original batch.")

    return {
        "action_batch": batch,
        "current_speaker": "Rules",
        "next": "End"
    }
