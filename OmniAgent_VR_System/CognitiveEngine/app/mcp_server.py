"""
File: mcp_server.py
Purpose: MCP Resource Layer.
Exposes Engine Capabilities (agent_move, agent_speak) as MCP Tools that return strict ActionBatch JSON.
This allows external agents or debuggers to control the UE5 Engine directly.
"""
from mcp.server.fastmcp import FastMCP
from typing import List
import json
from .schemas.actions import ActionBatch, GameAction
from .schemas.game_state import Vector3D

# Initialize FastMCP Server
mcp = FastMCP("OmniAgent-VR-Resource-Layer")

@mcp.resource("game://state/player")
def get_player_state() -> str:
    """Returns the current player state as a JSON string."""
    return '{"health": 100, "inventory": []}'

@mcp.tool()
def agent_speak(text: str, emotion: str = "Neutral") -> str:
    """
    Directly commands the agent to speak via Engine.
    Useful for debugging or direct overrides.
    """
    action = GameAction(
        action_type="Speak",
        parameters={"text": text, "emotion": emotion}
    )
    batch = ActionBatch(
        agent_id="MCP_Tool_Override",
        actions=[action],
        reasoning="Direct MCP Tool Call: agent_speak"
    )
    return batch.model_dump_json()

@mcp.tool()
def agent_move(x: float, y: float, z: float) -> str:
    """
    Commands the agent to move to a specific location.
    Validates coordinates via Vector3D.
    """
    # 1. Pydantic V2 Validation for Vector3D (NaN/Inf check)
    try:
        target_loc = Vector3D(x=x, y=y, z=z)
    except ValueError as e:
        return json.dumps({"error": f"Invalid Coordinates: {str(e)}"})

    # 2. Construct Action
    action = GameAction(
        action_type="Move",
        parameters={"speed": 500, "target_location": target_loc.model_dump()}
    )
    
    batch = ActionBatch(
        agent_id="MCP_Tool_Maps",
        actions=[action],
        reasoning="Direct MCP Tool Call: agent_move"
    )
    return batch.model_dump_json()

if __name__ == "__main__":
    mcp.run()
