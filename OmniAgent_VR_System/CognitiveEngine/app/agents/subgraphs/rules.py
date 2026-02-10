"""
File: rules.py
Purpose: Rules Agent (Referee) - Pure Python Validation.
Validates ActionBatch against game rules without LLM calls to save tokens.
"""
import json
from ..state import AgentState
from ...schemas.actions import ActionBatch, GameAction, WORLD_CONSTANTS


def validate_and_clamp_action(action) -> tuple:
    """
    Validates and clamps a single action's parameters.
    Returns: (modified_action, list_of_corrections)
    """
    corrections = []
    
    # Skip validation for SpeakAction (it doesn't have parameters)
    if action.action_type == "Speak":
        return action, corrections
    
    # Only validate GameAction types with parameters
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
