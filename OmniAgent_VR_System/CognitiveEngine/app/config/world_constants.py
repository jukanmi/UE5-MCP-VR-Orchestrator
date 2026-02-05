"""
File: world_constants.py
Purpose: Centralized world constants and game rules for the VR game.
All agents reference these constants to ensure consistency.
"""


# ============================================================================
# MOVEMENT CONSTANTS
# ============================================================================
MAX_SPEED = 600.0  # Unreal Units per second
WALK_SPEED = 200.0
RUN_SPEED = 400.0
CROUCH_SPEED = 100.0

# ============================================================================
# INTERACTION CONSTANTS
# ============================================================================
MAX_INTERACTION_DISTANCE = 300.0  # Unreal Units (cm)
PICKUP_DISTANCE = 150.0


# ============================================================================
# WORLD LIMITS
# ============================================================================
WORLD_HEIGHT_LIMIT = 10000.0  # Maximum Z coordinate
WORLD_BOUNDARY = 50000.0  # XY boundary

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================
def get_all_constants() -> dict:
    """
    Returns all world constants as a dictionary.
    Useful for injecting into LLM prompts.
    """
    return {
        "CRITICAL_HIT_MULTIPLIER": CRITICAL_HIT_MULTIPLIER,
        "MAX_SPEED": MAX_SPEED,
        "WALK_SPEED": WALK_SPEED,
        "RUN_SPEED": RUN_SPEED,
        "CROUCH_SPEED": CROUCH_SPEED,
        "MAX_INTERACTION_DISTANCE": MAX_INTERACTION_DISTANCE,
        "PICKUP_DISTANCE": PICKUP_DISTANCE,
        "WORLD_HEIGHT_LIMIT": WORLD_HEIGHT_LIMIT,
        "WORLD_BOUNDARY": WORLD_BOUNDARY,
    }


def format_constants_for_prompt() -> str:
    """
    Formats all constants as a human-readable string for LLM prompts.
    """
    constants = get_all_constants()
    lines = ["World Constants:"]
    
    # Group by category
    lines.append("\n[Combat]")
    lines.append(f"  CRITICAL_HIT_MULTIPLIER = {constants['CRITICAL_HIT_MULTIPLIER']}")
    
    lines.append("\n[Movement]")
    lines.append(f"  MAX_SPEED = {constants['MAX_SPEED']} UU/s")
    lines.append(f"  WALK_SPEED = {constants['WALK_SPEED']} UU/s")
    lines.append(f"  RUN_SPEED = {constants['RUN_SPEED']} UU/s")
    
    lines.append("\n[Interaction]")
    lines.append(f"  MAX_INTERACTION_DISTANCE = {constants['MAX_INTERACTION_DISTANCE']} UU")
    lines.append(f"  PICKUP_DISTANCE = {constants['PICKUP_DISTANCE']} UU")
    
    lines.append("\n[World Limits]")
    lines.append(f"  WORLD_HEIGHT_LIMIT = {constants['WORLD_HEIGHT_LIMIT']} UU")
    lines.append(f"  WORLD_BOUNDARY = {constants['WORLD_BOUNDARY']} UU")
    
    return "\n".join(lines)
