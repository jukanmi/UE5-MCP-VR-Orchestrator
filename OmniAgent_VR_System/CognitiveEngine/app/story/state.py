"""스토리 상태기계 — 결정론 뼈대. 비트 전이·서브 해금·플래그·대화 카운트를 파일에 보존한다.

LLM 은 여기 없다. 전이는 오직 complete_when 평가로만 일어나고, 디렉터(director.py)는
확정된 비트를 각색만 한다. 이 분리가 데모 중 엔딩 스킵/역행을 막는 핵심.
"""

import asyncio
import json
import logging
import os
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field

from .loader import END, Beat, StoryContent

logger = logging.getLogger(__name__)

SIDE_LOCKED, SIDE_AVAILABLE, SIDE_ACTIVE, SIDE_DONE = "locked", "available", "active", "done"


class StoryState(BaseModel):
    main_beat: str
    completed: List[str] = Field(default_factory=list)
    flags: Dict[str, bool] = Field(default_factory=dict)
    # 현재 비트 진입 이후 NPC 별 대화 턴 수. 전이 시 리셋 — 같은 NPC 를 요구하는 후속
    # 비트가 이전 카운트로 즉시 완료되는 것을 막는다.
    talk_counts: Dict[str, int] = Field(default_factory=dict)
    side: Dict[str, str] = Field(default_factory=dict)  # side id → locked|available|active|done
    director_cache: Dict[str, Any] = Field(default_factory=dict)  # {npc_goals, quest_log, side_surface}
    updated_at: str = ""


def _ci_eq(a: str, b: str) -> bool:
    return a.strip().casefold() == b.strip().casefold()


class StoryMachine:
    """content(작가 파일) + state(진행) + 파일 경로 + 락 한 묶음.

    on_trigger 와 save 는 하나의 asyncio.Lock 아래에서 돈다 — prompt 처리 중 combat_victory 가
    동시에 들어오면(FastAPI 메시지별 태스크) talk_counts/flags 가 서로 덮어쓰기 때문.
    """

    def __init__(self, content: StoryContent, state_path: Path):
        self.content = content
        self.state_path = state_path
        self.lock = asyncio.Lock()
        self.state = self._load()
        # 디렉터 재호출 필요 표시 — 비트 전이/해금(dirty) 또는 캐시 없음(첫 기동).
        self.needs_direction = not self.state.director_cache
        # 직전 디렉터 결과를 아직 UE5 응답에 실어 보내지 않았음. take_story_block 이 소비.
        # ponytail: 단일 UE5 클라이언트 가정 — 다중 클라이언트면 클라이언트별 큐 필요.
        self.block_pending = False

    # ── 파일 ──────────────────────────────────────────────────────
    def _load(self) -> StoryState:
        if self.state_path.exists():
            try:
                st = StoryState(**json.loads(self.state_path.read_text(encoding="utf-8")))
                if st.main_beat != END and st.main_beat not in self.content.beats:
                    raise ValueError(f"main_beat '{st.main_beat}' 가 현재 main.yaml 에 없음")
                # 콘텐츠에 새로 추가된 서브는 locked 로 채움
                for sid in self.content.sides:
                    st.side.setdefault(sid, SIDE_LOCKED)
                logger.info(f"[Story] 상태 복원: beat={st.main_beat}, completed={len(st.completed)}")
                return st
            except Exception as e:
                logger.warning(f"[Story] 상태 파일 손상 — 처음부터 시작: {e}")
        return StoryState(
            main_beat=self.content.main.start,
            side={sid: SIDE_LOCKED for sid in self.content.sides},
        )

    def save(self) -> None:
        """atomic: tmp 에 쓰고 rename. 중간에 죽어도 반쪽 파일이 남지 않는다."""
        self.state.updated_at = time.strftime("%Y-%m-%dT%H:%M:%S")
        tmp = self.state_path.with_suffix(".json.tmp")
        tmp.write_text(self.state.model_dump_json(indent=1), encoding="utf-8")
        os.replace(tmp, self.state_path)

    # ── 조회 ──────────────────────────────────────────────────────
    @property
    def current_beat(self) -> Optional[Beat]:
        return self.content.beat(self.state.main_beat)

    @property
    def ended(self) -> bool:
        return self.state.main_beat == END

    def available_sides(self) -> List[str]:
        return [sid for sid, s in self.state.side.items() if s in (SIDE_AVAILABLE, SIDE_ACTIVE)]

    def story_block(self) -> dict:
        """응답 JSON 최상위 `Story` 블록 (PascalCase 키 아래 snake_case — NpcPlans 규약 동일)."""
        beat = self.current_beat
        cache = self.state.director_cache
        return {
            "beat_id": self.state.main_beat,
            "quest_log": cache.get("quest_log") or (beat.quest_log if beat else "메인 퀘스트 완료"),
            "side": [sid for sid, s in self.state.side.items() if s == SIDE_ACTIVE],
            "events": list(beat.events) if beat else [],
        }

    def take_story_block(self) -> Optional[dict]:
        """디렉터 갱신 후 첫 응답에만 Story 블록을 싣는다(dirty 아닐 땐 생략)."""
        if not self.block_pending:
            return None
        self.block_pending = False
        return self.story_block()

    def directive_for(self, npc_ids: List[str]) -> Dict[str, dict]:
        """캐시된 디렉터 goals 중 npc_ids 에 해당하는 것만 (대소문자 무시). Stage2 주입용."""
        goals: Dict[str, dict] = self.state.director_cache.get("npc_goals") or {}
        out = {}
        for npc in npc_ids:
            for k, v in goals.items():
                if _ci_eq(k, npc):
                    out[npc] = v
        return out

    # ── 전이 ──────────────────────────────────────────────────────
    def _satisfied(self, beat: Beat, kind: str, data: dict) -> bool:
        cw = beat.complete_when
        if kind == "combat_victory":
            return cw.type == "boss_killed" and _ci_eq(cw.boss_id, data.get("target_id", ""))
        if kind == "talked_to":
            npc = data.get("npc_id", "")
            return (
                cw.type == "talked_to"
                and _ci_eq(cw.npc_id, npc)
                and self.state.talk_counts.get(cw.npc_id, 0) >= cw.min_turns
            )
        if kind == "flag":
            return cw.type == "flag" and cw.name == data.get("name") and self.state.flags.get(cw.name, False)
        return False

    async def on_trigger(self, kind: str, data: dict) -> bool:
        """트리거 반영 → 전이 평가 → 저장. dirty(비트 전이·서브 상태 변화) 여부 반환."""
        async with self.lock:
            st = self.state
            if kind == "talked_to":
                npc = data.get("npc_id") or ""
                if not npc:
                    return False
                # 카운트 키는 작가가 쓴 npc_id 철자로 정규화 — 조회 일관성.
                all_beats = list(self.content.beats.values()) + list(self.content.sides.values())
                author_ids = [b.complete_when.npc_id for b in all_beats]
                key = next((a for a in author_ids if _ci_eq(a, npc)), npc)
                st.talk_counts[key] = st.talk_counts.get(key, 0) + 1
            elif kind == "flag":
                st.flags[data["name"]] = True

            dirty = False
            beat = self.current_beat
            if beat and self._satisfied(beat, kind, data):
                self._advance(beat)
                dirty = True

            # 서브 — available/active 인 것만 완료 평가 (체인 next 는 side 안에서 진행)
            for sid in list(st.side):
                if st.side[sid] in (SIDE_AVAILABLE, SIDE_ACTIVE) and self._satisfied(
                    self.content.sides[sid], kind, data
                ):
                    st.side[sid] = SIDE_DONE
                    nxt = self.content.sides[sid].next
                    if nxt != END:
                        st.side[nxt] = SIDE_AVAILABLE
                    logger.info(f"[Story] 서브 완료: {sid}")
                    dirty = True

            if dirty:
                self.needs_direction = True
            self.save()
            return dirty

    def _advance(self, beat: Beat) -> None:
        st = self.state
        st.completed.append(beat.id)
        st.main_beat = beat.next
        st.talk_counts = {}
        for sid in beat.unlocks_side:
            if st.side.get(sid) == SIDE_LOCKED:
                st.side[sid] = SIDE_AVAILABLE
        logger.info(f"[Story] 비트 전이: {beat.id} → {beat.next} (해금 {beat.unlocks_side})")

    async def apply_direction(self, direction: dict) -> None:
        """디렉터 결과 캐시 + side_surface 를 active 로 승격 + 저장."""
        async with self.lock:
            for sid in direction.get("side_surface", []):
                if self.state.side.get(sid) == SIDE_AVAILABLE:
                    self.state.side[sid] = SIDE_ACTIVE
            self.state.director_cache = direction
            self.needs_direction = False
            self.block_pending = True
            self.save()
