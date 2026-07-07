import asyncio
import json
import httpx
import websockets

async def manual_affinity_smoke():
    uri = "ws://127.0.0.1:8000/ws/ue5"
    async with websockets.connect(uri) as websocket:
        print("[TEST] Connected to WebSocket.")
        
        # 1. Player says something nice to Elara
        payload = {
            "msg_id": "test_affinity_001",
            "type": "prompt",
            "auth_token": "omniagent-dev-secret-changeme-before-production",
            "payload": {
                "player_id": "Player",
                "voice_transcript": "Hello Elara! You look wonderful today.",
                "gestures": [],
                "timestamp": 12345.0,
                "looking_at_entity_id": "Elara",
                "player_location": {"x": 0.0, "y": 0.0, "z": 0.0}
            }
        }
        await websocket.send(json.dumps(payload))
        print(f"[TEST] Sent prompt: {payload['payload']['voice_transcript']}")
        
        # Wait for response
        try:
            response = await asyncio.wait_for(websocket.recv(), timeout=15.0)
            data = json.loads(response)
            print(f"\n[TEST] Received response:\n{json.dumps(data, indent=2, ensure_ascii=False)}")
        except asyncio.TimeoutError:
            print("[TEST] Timeout waiting for response 1")
            
        
        # 2. Player attacks Elara verbally
        payload2 = {
            "msg_id": "test_affinity_002",
            "type": "prompt",
            "auth_token": "omniagent-dev-secret-changeme-before-production",
            "payload": {
                "player_id": "Player",
                "voice_transcript": "I hate you, I'm going to kill you!",
                "gestures": [],
                "timestamp": 12346.0,
                "looking_at_entity_id": "Elara",
                "player_location": {"x": 0.0, "y": 0.0, "z": 0.0}
            }
        }
        await websocket.send(json.dumps(payload2))
        print(f"\n[TEST] Sent prompt: {payload2['payload']['voice_transcript']}")
        
        # Wait for response
        try:
            response2 = await asyncio.wait_for(websocket.recv(), timeout=15.0)
            data2 = json.loads(response2)
            print(f"\n[TEST] Received response:\n{json.dumps(data2, indent=2, ensure_ascii=False)}")
        except asyncio.TimeoutError:
            print("[TEST] Timeout waiting for response 2")
        print(f"\n[TEST] Sent prompt: {payload2['payload']['voice_transcript']}")
        
        # Wait for response
        response2 = await websocket.recv()
        data2 = json.loads(response2)
        print(f"\n[TEST] Received response:\n{json.dumps(data2, indent=2, ensure_ascii=False)}")

if __name__ == "__main__":
    asyncio.run(manual_affinity_smoke())
