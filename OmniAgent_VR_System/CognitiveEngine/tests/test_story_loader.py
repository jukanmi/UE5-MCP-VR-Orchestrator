"""비트 시트 로더 검증 — 참조 무결성 실패는 비트 id 를 담은 ValueError."""

import copy

import pytest

from app.story.loader import load_content
from story_fixtures import MAIN, SIDE, write_content


def test_loads_dummy_content(tmp_path):
    c = load_content(write_content(tmp_path))
    assert c.main.start == "b1" and set(c.beats) == {"b1", "b2", "b3"} and "s1" in c.sides


def test_dangling_next_fails_with_beat_id(tmp_path):
    main = copy.deepcopy(MAIN)
    main["beats"][0]["next"] = "b_missing"
    with pytest.raises(ValueError, match="b1.*b_missing"):
        load_content(write_content(tmp_path, main))


def test_unknown_side_and_bad_complete_when(tmp_path):
    main = copy.deepcopy(MAIN)
    main["beats"][0]["unlocks_side"] = ["s_ghost"]
    with pytest.raises(ValueError, match="s_ghost"):
        load_content(write_content(tmp_path, main))

    main = copy.deepcopy(MAIN)
    main["beats"][2]["complete_when"] = {"type": "boss_killed"}  # boss_id 누락
    with pytest.raises(ValueError, match="boss_id"):
        load_content(write_content(tmp_path, main))

    side = copy.deepcopy(SIDE)
    side["available_after"] = "nope"
    with pytest.raises(ValueError, match="s1.*nope"):
        load_content(write_content(tmp_path, MAIN, side))


def test_shipped_content_is_valid():
    """리포 동봉 content/ 가 항상 로드 가능해야 서버가 뜬다."""
    from app.story import CONTENT_DIR

    c = load_content(CONTENT_DIR)
    assert c.main.start in c.beats


def test_shipped_npc_goals_have_scenario_personas():
    """비트 시트가 지목한 NPC 마다 content/npcs/<npc>/persona.yaml 이 있어야 seed 후 Stage1 페르소나가 맞물린다."""
    from app.story import CONTENT_DIR

    c = load_content(CONTENT_DIR)
    beats = list(c.beats.values()) + list(c.sides.values())
    npcs = {npc for b in beats for npc in b.npc_goals} | {b.complete_when.npc_id for b in beats if b.complete_when.npc_id}
    missing = [n for n in npcs if not (CONTENT_DIR / "npcs" / n.lower() / "persona.yaml").exists()]
    assert not missing, f"시나리오 페르소나 없음: {missing}"
