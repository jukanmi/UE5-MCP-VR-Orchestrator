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
# COMBAT CONSTANTS
# ============================================================================
CRITICAL_HIT_MULTIPLIER = 1.5

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
