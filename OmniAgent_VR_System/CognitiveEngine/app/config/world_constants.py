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
