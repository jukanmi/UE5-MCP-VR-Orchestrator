"""대상 NPC 라우팅 — UE5 명시 대상이 발화 속 이름 언급보다 우선한다.

실측 사고(2026-09-18 전 루프 주행): Moca 를 마주 보고 "James 가 당신을 찾으라고 했어요" 라고 말하자
James 가 답하고 talked_to 는 Moca 에 적립됐다. 이름 추출은 명시 대상이 없을 때(HUD 자유 채팅)만 쓴다.
"""

from app.agents.interface_input import interface_input_node


def _state(transcript: str, explicit: str | None) -> dict:
    s = {"vr_context": {"player_id": "P", "voice_transcript": transcript, "target_npc_id": explicit, "timestamp": 0.0}}
    if explicit:
        s["target_npc"] = explicit
        s["target_npcs"] = [explicit]
    return s


def test_explicit_target_wins_over_name_mention():
    out = interface_input_node(_state("James 가 당신을 찾으라고 했어요.", "Moca"))
    assert out["target_npcs"] == ["Moca"]
    assert out["target_npc"] == "Moca"


def test_name_extraction_only_without_explicit_target():
    out = interface_input_node(_state("James, 여기 좀 봐.", None))
    assert out.get("target_npcs") == ["James"]


def test_no_target_no_name_keeps_state_untouched():
    out = interface_input_node(_state("안녕하세요.", None))
    assert "target_npcs" not in out
