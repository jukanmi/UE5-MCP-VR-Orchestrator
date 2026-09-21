"""스토리 테스트 공용 — 임시 디렉토리에 더미 비트 시트를 쓰고 StoryMachine 을 만든다."""

from pathlib import Path

import yaml

from app.story.loader import load_content
from app.story.state import StoryMachine

MAIN = {
    "title": "t",
    "start": "b1",
    "beats": [
        {
            "id": "b1",
            "title": "도착",
            "summary": "s1",
            "npc_goals": {"Elara": "용 이야기를 흘린다", "Guard": "무기를 권한다"},
            "quest_log": "q1",
            "complete_when": {"type": "talked_to", "npc_id": "Elara", "min_turns": 2},
            "next": "b2",
            "unlocks_side": ["s1"],
        },
        {
            "id": "b2",
            "title": "준비",
            "npc_goals": {"Guard": "검을 준다"},
            "quest_log": "q2",
            "complete_when": {"type": "flag", "name": "entered_cave"},
            "next": "b3",
        },
        {
            "id": "b3",
            "title": "보스",
            "npc_goals": {"Guard": "함께 싸운다"},
            "quest_log": "q3",
            "complete_when": {"type": "boss_killed", "boss_id": "DragonBoss"},
            "next": "end",
        },
    ],
}
SIDE = {
    "id": "s1",
    "title": "동생",
    "available_after": "b1",
    "npc_goals": {"Elara": "동생을 찾아 달라"},
    "quest_log": "sq1",
    "complete_when": {"type": "flag", "name": "found_brother"},
}


def write_content(tmp: Path, main: dict = MAIN, side: dict | None = SIDE) -> Path:
    content = tmp / "content"
    (content / "side").mkdir(parents=True, exist_ok=True)
    (content / "main.yaml").write_text(yaml.safe_dump(main, allow_unicode=True), encoding="utf-8")
    if side:
        (content / "side" / "s.yaml").write_text(yaml.safe_dump(side, allow_unicode=True), encoding="utf-8")
    return content


def make_machine(tmp: Path) -> StoryMachine:
    return StoryMachine(load_content(write_content(tmp)), tmp / "story_state.json")
