from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from .schemas.vr_context import GesPrompt
from .schemas.actions import ActionBatch
import json

app = FastAPI()

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
                ges_prompt = GesPrompt(**input_data)
                print(f"Received valid GesPrompt from player: {ges_prompt.player_id}")
                print(f"Transcript: {ges_prompt.voice_transcript}")
                
                # TODO: Pass to Orchestrator / LangGraph here
                # response = orchestrator.process(ges_prompt)
                
                # Mock Response for Phase 1
                mock_response = {
                    "agent_id": "Supervisor",
                    "actions": [
                        {
                            "action_type": "Speak",
                            "parameters": {"text": f"I heard you say: {ges_prompt.voice_transcript}"}
                        }
                    ]
                }
                
                # Validate output before sending
                valid_action = ActionBatch(**mock_response)
                
                await websocket.send_text(valid_action.model_dump_json())
                
            except Exception as e:
                print(f"Validation or Processing Error: {e}")
                await websocket.send_text(json.dumps({"error": str(e)}))
                
    except WebSocketDisconnect:
        print("UE5 Client Disconnected")
