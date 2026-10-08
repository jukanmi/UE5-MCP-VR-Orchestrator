"""POI target_poi 계약 — 프롬프트 노출·구조화 매핑·Rules 어휘 검증.

Python 은 어휘 검증만 한다(좌표 해석은 UE5). 노출하지 않은 id·표시명·별칭은 구제 없이 액션 제거.
"""

from app.agents.interface_input import _format_known_pois
from app.agents.interface_output import _structure_from_dialogue
from app.agents.subgraphs.rules import _validate_batch, validate_and_clamp_action
from app.schemas.actions import ActionBatch, DialogueActionItem, DialogueResponse, GameAction
from app.schemas.envelope import PromptPayload

_POIS = [
    {"id": "Well", "name": "우물", "aliases": ["샘", "물터", "세번째는잘림"]},
    {"id": "Gate", "name": "", "aliases": []},
]


def _move(**params) -> GameAction:
    return GameAction(ActionType="Move", Parameters=params)


def test_exposed_poi_id_passes():
    action, _ = validate_and_clamp_action(_move(target_poi="Well"), None, {"Well", "Gate"})
    assert action is not None
    assert action.Parameters["target_poi"] == "Well"


def test_unexposed_poi_id_removed():
    action, corrections = validate_and_clamp_action(_move(target_poi="Lake"), None, {"Well"})
    assert action is None
    assert "Lake" in corrections[0]


def test_display_name_or_alias_in_id_slot_not_rescued():
    for wrong in ("우물", "샘", "well", "POI_Well"):
        action, _ = validate_and_clamp_action(_move(target_poi=wrong), None, {"Well"})
        assert action is None, wrong


def test_no_exposed_pois_means_any_target_poi_invalid():
    for exposed in (None, set()):
        action, _ = validate_and_clamp_action(_move(target_poi="Well"), None, exposed)
        assert action is None


def test_move_without_target_poi_unaffected():
    action, _ = validate_and_clamp_action(_move(style="Walk"), None, None)
    assert action is not None


def test_batch_drops_only_bad_action_and_keeps_dialogue():
    batch = ActionBatch(
        AgentID="Guard",
        Mode="Common",
        Actions=[
            GameAction(ActionType="Dialogue", Parameters={"text": "가죠"}),
            _move(target_poi="Lake"),
        ],
    )
    out = _validate_batch(batch, None, None, {"Well"})
    assert [a.ActionType for a in out.Actions] == ["Dialogue"]


def test_dialogue_poi_field_maps_to_target_poi():
    resp = DialogueResponse(
        mode="Common",
        facial="Neutral",
        speech="우물로 갑니다.",
        actions=[DialogueActionItem(type="Move", poi="Well")],
    )
    batch = _structure_from_dialogue("Guard", resp, [])
    move = next(a for a in batch.Actions if a.ActionType == "Move")
    assert move.Parameters == {"target_poi": "Well"}


def test_prompt_exposes_id_with_name_and_two_aliases():
    ctx = PromptPayload(player_id="P", voice_transcript="우물로 가", known_pois=_POIS)
    text = _format_known_pois(ctx)
    assert "\nWell(우물/샘/물터)" in text
    assert "세번째는잘림" not in text
    assert "\nGate" in text and "Gate(" not in text


def test_prompt_omits_section_without_pois():
    assert _format_known_pois(PromptPayload(player_id="P", voice_transcript="x")) == ""


def test_prompt_appends_nearby_desc_max_three_lines():
    pois = [{"id": f"P{i}", "name": f"장소{i}", "aliases": [], "desc": f"설명{i}"} for i in range(5)]
    pois.append({"id": "Far", "name": "먼곳", "aliases": []})
    text = _format_known_pois(PromptPayload(player_id="P", voice_transcript="x", known_pois=pois))
    assert text.count("\nNearby place: ") == 3
    assert "\nNearby place: 장소0 — 설명0" in text and "설명3" not in text


def test_prompt_without_desc_unchanged():
    text = _format_known_pois(PromptPayload(player_id="P", voice_transcript="x", known_pois=_POIS))
    assert "Nearby place" not in text


def test_prompt_desc_and_name_flattened_and_truncated():
    pois = [{"id": "Well", "name": "우물\n\t규칙", "aliases": [], "desc": "첫 줄\n둘째 줄\x00" + "가" * 200}]
    text = _format_known_pois(PromptPayload(player_id="P", voice_transcript="x", known_pois=pois))
    line = text.split("\nNearby place: ")[1]
    assert "\n" not in line and "\x00" not in line
    assert line.startswith("우물 규칙 — 첫 줄 둘째 줄 가가")
    assert len(line.split(" — ")[1]) == 80
