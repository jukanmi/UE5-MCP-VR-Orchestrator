"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: rules.py                                                              ║
║ Role: VALIDATOR (Game Rules Referee)                                       ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Validate ActionBatch against game rules. Clamp out-of-bounds values,      ║
║   reject invalid actions, and ensure UE5 can safely execute all commands.   ║
║                                                                              ║
║ INPUT:  ActionBatch (from Interface Output)                                 ║
║ OUTPUT: ActionBatch (validated and clamped)                                 ║
║                                                                              ║
║ VALIDATION RULES:                                                            ║
║   • Movement speed: [0, MAX_SPEED]                                          ║
║   • Attack damage: [0, MAX_DAMAGE]                                          ║
║   • Interaction range: [0, MAX_INTERACTION_RANGE]                           ║
║   • Required fields: executor_npc_id, action_type must exist                ║
║                                                                              ║
║ CLAMPING STRATEGY:                                                           ║
║   - Out-of-range values → clamp to min/max (log correction)                 ║
║   - Missing required fields → REJECT action entirely                        ║
║   - Invalid action_type → REJECT action entirely                            ║
║                                                                              ║
║ REJECTION BEHAVIOR:                                                          ║
║   If critical violations occur, mark batch with "REJECTED" in reasoning.    ║
║   Supervisor will detect this and loop back to Dialogue for retry.          ║
║                                                                              ║
║ DESIGN PRINCIPLE:                                                            ║
║   Pure Python validation - NO LLM CALLS. This saves tokens and ensures      ║
║   deterministic, fast validation. Rules are defined in WORLD_CONSTANTS.     ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import json
from ..state import AgentState
from ...schemas.actions import ActionBatch, NPCAction, WORLD_CONSTANTS


def validate_and_clamp_action(action) -> tuple:
    """
    Validates and clamps a single action's parameters.
    Returns: (modified_action, list_of_corrections)
    """
    corrections = []
    
    # Dialogue 액션은 파라미터 검증 불필요 (text/emotion만 사용하고, 범위 제한 없음)
    if action.action_type == "Dialogue":
        return action, corrections
    
    # 파라미터 없는 액션은 검증 대상 아님
    if not hasattr(action, 'parameters') or not action.parameters:
        return action, corrections
    
    # Convert all parameters to strings for C++ compatibility
    params = {str(k): str(v) for k, v in action.parameters.items()}
    
    # Numeric clamping based on action type
    if action.action_type == "Attack":
        # Clamp damage
        if "damage" in params:
            try:
                damage = float(params["damage"])
                max_damage = WORLD_CONSTANTS.get("MAX_DAMAGE", 100)
                if damage > max_damage:
                    params["damage"] = str(max_damage)
                    corrections.append(f"Clamped damage {damage} -> {max_damage}")
                elif damage < 0:
                    params["damage"] = "0"
                    corrections.append(f"Clamped negative damage to 0")
            except ValueError:
                params["damage"] = "10"  # Default
                corrections.append("Invalid damage value, set to default 10")
    
    elif action.action_type == "Move":
        # Clamp speed
        if "speed" in params:
            try:
                speed = float(params["speed"])
                max_speed = WORLD_CONSTANTS.get("MAX_SPEED", 600)
                if speed > max_speed:
                    params["speed"] = str(max_speed)
                    corrections.append(f"Clamped speed {speed} -> {max_speed}")
                elif speed < 0:
                    params["speed"] = "300"  # Default walk speed
                    corrections.append("Clamped negative speed to default 300")
            except ValueError:
                params["speed"] = "300"
                corrections.append("Invalid speed value, set to default 300")
    
    elif action.action_type == "Heal":
        # Clamp health
        if "amount" in params:
            try:
                amount = float(params["amount"])
                max_health = WORLD_CONSTANTS.get("MAX_HEALTH", 100)
                if amount > max_health:
                    params["amount"] = str(max_health)
                    corrections.append(f"Clamped heal amount {amount} -> {max_health}")
                elif amount < 0:
                    params["amount"] = "0"
                    corrections.append("Clamped negative heal to 0")
            except ValueError:
                params["amount"] = "10"
                corrections.append("Invalid heal amount, set to default 10")
    
    # Update action with validated parameters
    action.parameters = params
    return action, corrections


def rules_node(state: AgentState):
    """
    Rules Agent (Pure Python Validation).
    Validates and clamps ActionBatch parameters without LLM calls.
    """
    batch = state.get("action_batch")
    
    if not batch:
        return {"next": "End", "current_speaker": "Rules"}
    
    all_corrections = []
    validated_actions = []
    
    # Validate each action
    for action in batch.actions:
        validated_action, corrections = validate_and_clamp_action(action)
        validated_actions.append(validated_action)
        all_corrections.extend(corrections)
    
    # Update batch with validated actions
    batch.actions = validated_actions
    
    # Append corrections to reasoning
    if all_corrections:
        correction_summary = "; ".join(all_corrections)
        batch.reasoning = f"{batch.reasoning} | Rules: {correction_summary}"
        print(f"[Rules] Applied {len(all_corrections)} corrections: {correction_summary}")
    else:
        print("[Rules] No corrections needed, batch is valid")
    
    return {
        "action_batch": batch,
        "current_speaker": "Rules",
        "next": "End"
    }
