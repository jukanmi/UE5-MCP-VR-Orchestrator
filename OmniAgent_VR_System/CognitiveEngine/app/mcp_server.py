from mcp.server.fastmcp import FastMCP

# Initialize FastMCP Server
mcp = FastMCP("OmniAgent-VR-Resource-Layer")

@mcp.resource("game://state/player")
def get_player_state() -> str:
    """Returns the current player state as a JSON string."""
    return '{"health": 100, "inventory": []}'

if __name__ == "__main__":
    mcp.run()
