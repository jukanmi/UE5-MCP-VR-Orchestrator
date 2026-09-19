"""작가 비트 시트(YAML) 로더 — main.yaml + side/*.yaml 을 읽고 참조 무결성을 검증한다.

실패는 서버 기동 시 ValueError 로 즉시 드러낸다(비트 id 포함). 런타임에 "다음 비트 없음" 으로
조용히 멈추는 것보다 기동 실패가 낫다.
"""

from pathlib import Path
from typing import Dict, List, Literal, Optional

import yaml
from pydantic import BaseModel, Field, model_validator

END = "end"  # next 종착 센티넬


class CompleteWhen(BaseModel):
    type: Literal["talked_to", "boss_killed", "flag"]
    npc_id: str = ""  # talked_to
    min_turns: int = 1  # talked_to
    boss_id: str = ""  # boss_killed
    name: str = ""  # flag

    @model_validator(mode="after")
    def _required_by_type(self):
        need = {"talked_to": "npc_id", "boss_killed": "boss_id", "flag": "name"}[self.type]
        if not getattr(self, need):
            raise ValueError(f"complete_when.type={self.type} 는 '{need}' 필수")
        return self


class Beat(BaseModel):
    id: str
    title: str = ""
    summary: str = ""  # 디렉터 컨텍스트
    npc_goals: Dict[str, str] = Field(default_factory=dict)  # 작가 초안 — LLM 폴백 원문
    quest_log: str = ""
    quest_target_tag: str = ""
    complete_when: CompleteWhen
    next: str = END
    unlocks_side: List[str] = Field(default_factory=list)
    events: List[dict] = Field(default_factory=list)  # Phase C. 파싱만, 실행은 UE5


class MainStory(BaseModel):
    title: str = ""
    start: str
    beats: List[Beat]


class SideQuest(Beat):
    available_after: str  # 해금 기준 main beat id (unlocks_side 와 이중 안전)


class StoryContent(BaseModel):
    main: MainStory
    sides: Dict[str, SideQuest] = Field(default_factory=dict)
    beats: Dict[str, Beat] = Field(default_factory=dict)  # main beat id → Beat

    def beat(self, beat_id: str) -> Optional[Beat]:
        return self.beats.get(beat_id)


def _load_yaml(path: Path) -> dict:
    with path.open(encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def load_content(content_dir: Path) -> StoryContent:
    """content_dir/main.yaml + content_dir/side/*.yaml → StoryContent. 참조 오류는 ValueError."""
    main = MainStory(**_load_yaml(content_dir / "main.yaml"))
    beats = {b.id: b for b in main.beats}
    if len(beats) != len(main.beats):
        raise ValueError("main.yaml: 중복 beat id")

    sides: Dict[str, SideQuest] = {}
    side_dir = content_dir / "side"
    for p in sorted(side_dir.glob("*.yaml")) if side_dir.is_dir() else []:
        sq = SideQuest(**_load_yaml(p))
        if sq.id in sides:
            raise ValueError(f"side: 중복 id '{sq.id}' ({p.name})")
        sides[sq.id] = sq

    # ── 참조 무결성 ────────────────────────────────────────────────
    if main.start not in beats:
        raise ValueError(f"main.yaml: start='{main.start}' 가 beats 에 없음")
    for b in main.beats:
        if b.next != END and b.next not in beats:
            raise ValueError(f"beat '{b.id}': next='{b.next}' 가 beats 에 없음")
        for sid in b.unlocks_side:
            if sid not in sides:
                raise ValueError(f"beat '{b.id}': unlocks_side '{sid}' 가 side/ 에 없음")
    for sq in sides.values():
        if sq.available_after not in beats:
            raise ValueError(f"side '{sq.id}': available_after='{sq.available_after}' 가 beats 에 없음")
        if sq.next != END and sq.next not in sides:
            raise ValueError(f"side '{sq.id}': next='{sq.next}' 가 side/ 에 없음")

    return StoryContent(main=main, sides=sides, beats=beats)
