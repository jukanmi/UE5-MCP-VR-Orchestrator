"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: main.py                                                               ║
║ Role: FASTAPI ENTRY POINT (UE5 ↔ Cognitive Engine Bridge)                  ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Manage WebSocket connection with Unreal Engine 5. Receive GesPrompt,     ║
║   invoke LangGraph workflow, and return ActionBatch to UE5.                 ║
║                                                                              ║
║ REQUEST-RESPONSE CYCLE:                                                     ║
║   1. UE5 sends JSON via WebSocket                                           ║
║   2. Parse and validate as GesPrompt (Pydantic)                             ║
║   3. Build initial AgentState                                               ║
║   4. Invoke app_graph.invoke(state)                                         ║
║   5. Extract action_batch from result                                       ║
║   6. Serialize and send back to UE5                                         ║
║                                                                              ║
║ ERROR HANDLING:                                                              ║
║   - JSON parse errors → send {"error": "message"}                           ║
║   - Pydantic validation errors → send error details                         ║
║   - Empty action_batch → send minimal error batch                           ║
║   - Graph exceptions → full traceback printed, error sent to UE5            ║
║                                                                              ║
║ ENDPOINTS:                                                                   ║
║   • GET  /          - Health check                                          ║
║   • WS   /ws/ue5    - Main WebSocket for game communication                 ║
║                                                                              ║
║ PROTOCOL:                                                                    ║
║   All communication is JSON-formatted. GesPrompt and ActionBatch schemas    ║
║   define the contract between UE5 and Cognitive Engine.                     ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from .schemas.vr_context import GesPrompt
from .schemas.actions import ActionBatch
from .schemas.behavior_policy import BehaviorPolicy, PatchPolicy
from typing import Union
from .agents.state import AgentState
from .graph import app_graph
import json

app = FastAPI()

def serialize_policy_update(policy: Union[BehaviorPolicy, PatchPolicy]) -> str:
    """Helper to envelope a policy update for UE consumption."""
    return json.dumps({
        "type": "policy_update",
        "payload": policy.model_dump()
    })


@app.get("/")
async def root():
    return {"message": "OmniAgent Cognitive Engine is running"}

@app.websocket("/ws/ue5")
async def websocket_ue5_endpoint(websocket: WebSocket):
    await websocket.accept()
    print("UE5 Client Connected")
    try:
        while True:
            # Receive raw JSON from UE5
            data = await websocket.receive_text()
            
            try:
                # 1. Validation (Strict Pydantic)
                input_data = json.loads(data)
                print(f"[Main] Raw input_data: {input_data}")
                
                ges_prompt = GesPrompt(**input_data)
                print(f"[Main] Parsed GesPrompt: player_id={ges_prompt.player_id}, transcript={ges_prompt.voice_transcript}")
                print(f"[Main] GesPrompt player_location: {ges_prompt.player_location}")
                
                # 2. Build Initial State (Section 8 Orchestra)
                initial_state = AgentState(
                    messages=[],
                    vr_context=ges_prompt,
                    game_state=None,
                    next="",
                    current_speaker="",
                    analysis={},
                    # Section 8: Orchestra pipeline fields
                    natural_context=None,
                    raw_response=None,
                    target_npc=None,
                    action_batch=None
                )
                print(f"[Main] initial_state vr_context: {initial_state.get('vr_context')}")
                
                # 3. Run Graph
                print("Running Graph...")
                result = app_graph.invoke(initial_state)
                
                # 4. Extract Output
                final_action = result.get("action_batch")
                
                if final_action:
                    print(f"Graph Produced Action: {final_action.model_dump_json()}")
                    await websocket.send_text(final_action.model_dump_json())
                else:
                    # No action generated (maybe just internal thought or error)
                    error_batch = {
                        "agent_id": "System",
                        "actions": [],
                        "reasoning": "No action generated by agents."
                    }
                    await websocket.send_text(json.dumps(error_batch))
                
            except Exception as e:
                print(f"Processing Error: {e}")
                import traceback
                traceback.print_exc()
                await websocket.send_text(json.dumps({"error": str(e)}))
                
    except WebSocketDisconnect:
        print("UE5 Client Disconnected")
