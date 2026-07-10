import sys
import asyncio
import traceback

from pathlib import Path
sys.path.append(str(Path(__file__).resolve().parents[1]))

# 수동 라이브 파이프라인 하네스 — pytest 유닛이 아님(test_ 함수 없음, 서버/LLM 필요).
# `python -m tests.test_pipeline` 로 직접 실행. LocationData 폐기 → Vector3D 사용.
from app.main import _handle_prompt
from app.schemas.game_state import Vector3D
from app.schemas.envelope import MessageEnvelope, PromptPayload


async def main():
    try:
        payload = PromptPayload(
            player_id="Player1",
            voice_transcript="Hello Elara, how are you today?",
            looking_at_entity_id="Elara",
            player_location=Vector3D(x=100, y=200, z=0),
            last_event="None",
            agent_status={"Elara": "Idle"},
            stats={"health": 100},
            gestures=[{"gesture_type": "Wave", "confidence": 1.0}],
        )

        envelope = MessageEnvelope(
            type="prompt", auth_token="dummy", msg_id="test1", timestamp=0.0, payload={"prompt": payload.model_dump()}
        )

        result = await _handle_prompt(envelope)
        print("PIPELINE RESULT:", result)
    except Exception:
        print("PIPELINE ERROR:")
        traceback.print_exc()


if __name__ == "__main__":
    asyncio.run(main())
