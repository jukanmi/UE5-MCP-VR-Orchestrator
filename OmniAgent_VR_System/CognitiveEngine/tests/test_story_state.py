"""상태기계 전이 — boss / talked_to / flag / 서브 해금 / 파일 보존."""

import asyncio

from app.story.loader import load_content
from app.story.state import SIDE_AVAILABLE, SIDE_DONE, SIDE_LOCKED, StoryMachine
from story_fixtures import make_machine


def _t(m, kind, data):
    return asyncio.run(m.on_trigger(kind, data))


def test_talked_to_needs_min_turns(tmp_path):
    m = make_machine(tmp_path)
    assert _t(m, "talked_to", {"npc_id": "elara"}) is False  # 1회 — 무전이 (대소문자 무시)
    assert m.state.main_beat == "b1"
    assert _t(m, "talked_to", {"npc_id": "Elara"}) is True  # 2회 — 전이
    assert m.state.main_beat == "b2" and m.state.completed == ["b1"]
    assert m.state.talk_counts == {}  # 전이 시 리셋


def test_side_unlocks_only_on_transition(tmp_path):
    m = make_machine(tmp_path)
    assert m.state.side["s1"] == SIDE_LOCKED
    _t(m, "talked_to", {"npc_id": "Elara"})
    assert m.state.side["s1"] == SIDE_LOCKED  # 전이 없으면 불변
    _t(m, "talked_to", {"npc_id": "Elara"})
    assert m.state.side["s1"] == SIDE_AVAILABLE
    # 서브 자체 완료 — available 상태에서만 평가
    assert _t(m, "flag", {"name": "found_brother"}) is True
    assert m.state.side["s1"] == SIDE_DONE


def test_flag_transition(tmp_path):
    m = make_machine(tmp_path)
    m.state.main_beat = "b2"
    assert _t(m, "flag", {"name": "other"}) is False
    assert _t(m, "flag", {"name": "entered_cave"}) is True
    assert m.state.main_beat == "b3" and m.state.flags == {"other": True, "entered_cave": True}


def test_boss_killed_only_matching_id(tmp_path):
    m = make_machine(tmp_path)
    m.state.main_beat = "b3"
    assert _t(m, "combat_victory", {"target_id": "Bandit"}) is False
    assert m.state.main_beat == "b3"
    assert _t(m, "combat_victory", {"target_id": "DragonBoss"}) is True
    assert m.state.main_beat == "end" and m.ended
    assert _t(m, "combat_victory", {"target_id": "DragonBoss"}) is False  # 종료 후 무반응


def test_state_persists_across_restart(tmp_path):
    m = make_machine(tmp_path)
    _t(m, "talked_to", {"npc_id": "Elara"})
    _t(m, "talked_to", {"npc_id": "Elara"})
    assert (tmp_path / "story_state.json").exists()
    m2 = StoryMachine(load_content(tmp_path / "content"), tmp_path / "story_state.json")
    assert m2.state.main_beat == "b2" and m2.state.side["s1"] == SIDE_AVAILABLE


def test_concurrent_triggers_serialized(tmp_path):
    """락 — 동시 talked_to 10건이 전부 카운트되고 파일도 한 번에 하나씩 쓰인다."""
    m = make_machine(tmp_path)
    m.state.main_beat = "b2"  # talked_to 전이 없이 카운트만 누적

    async def burst():
        await asyncio.gather(*[m.on_trigger("talked_to", {"npc_id": "Elara"}) for _ in range(10)])

    asyncio.run(burst())
    assert m.state.talk_counts["Elara"] == 10
