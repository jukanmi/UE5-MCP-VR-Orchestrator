"""스토리 디렉터 LLM — 코드가 확정한 비트를 각색만 한다(비트 선택 없음).

호출 시점은 StoryMachine.needs_direction 일 때뿐(전이·해금·첫 기동). 실패하면 작가 YAML
원문을 그대로 쓴다 — 전이는 이미 결정론으로 끝났으므로 데모가 멈추지 않는다.
"""

import logging
from typing import Dict, List

from pydantic import AliasChoices, BaseModel, Field

from ..utils.llm_factory import STORY_MODEL, ollama_structured
from .state import StoryMachine

logger = logging.getLogger(__name__)

# 실측 중앙값 1.4s(2026-09-17). 기본 60s 는 클라우드 단절 시 그 턴 응답을 통째로 묶는다.
DIRECTOR_TIMEOUT_S = 5.0


class NPCGoalDirective(BaseModel):
    # 클라우드 모델은 grammar 미강제 — "npc" 로 줄여 쓰는 일이 있어(2026-09-18 실측) 별칭 허용.
    npc_id: str = Field(
        description="NPC id copied EXACTLY from the beat's npc_goals keys",
        validation_alias=AliasChoices("npc_id", "npc"),
    )
    goal: str = Field(description="What this NPC pursues now, one short Korean phrase")
    hint: str = Field(default="", description="One Korean sentence on how the NPC should steer dialogue")


class StoryDirectorResponse(BaseModel):
    npc_goals: List[NPCGoalDirective]
    quest_log: str = Field(description="One Korean sentence shown to the player as the current objective")
    side_surface: List[str] = Field(
        default_factory=list, description="Ids from AVAILABLE SIDE QUESTS to reveal to the player now"
    )


DIRECTOR_SYSTEM_PROMPT = """You are the story director of a VR game. The writer has fixed the plot; \
the CURRENT BEAT is already decided and you must NOT skip, reorder or invent beats.

Your job: adapt the writer's draft to what the player has actually done.
- npc_goals: one entry per NPC listed in the beat's draft. Keep the writer's intent, rephrase for the
  current situation. goal = 짧은 한국어 구절, hint = 대사 방향 한 문장.
- quest_log: 플레이어에게 보일 현재 목표 한 문장 (한국어).
- side_surface: ids from AVAILABLE SIDE QUESTS worth revealing now (may be empty). Never invent ids.

Use ONLY facts from the input. Respond ONLY with JSON of this exact shape (structure only — never copy values):
{"npc_goals":[{"npc_id":"<id>","goal":"<한국어 구절>","hint":"<한국어 한 문장>"}],"quest_log":"<한국어 한 문장>","side_surface":[]}"""


def _build_user_prompt(m: StoryMachine, reason: str) -> str:
    st, beat = m.state, m.current_beat
    prev = m.content.beat(st.completed[-1]) if st.completed else None
    lines = [f"TRIGGER: {reason}", "", f"CURRENT BEAT [{beat.id}] {beat.title}", f"summary: {beat.summary}"]
    lines.append("draft npc_goals:")
    lines += [f"  {npc}: {goal}" for npc, goal in beat.npc_goals.items()]
    lines.append(f"draft quest_log: {beat.quest_log}")
    if prev:
        lines += ["", f"PREVIOUS BEAT [{prev.id}] {prev.title}: {prev.summary}"]
    lines += ["", "AVAILABLE SIDE QUESTS:"]
    avail = m.available_sides()
    lines += [f"  {sid}: {m.content.sides[sid].title} — {m.content.sides[sid].summary}" for sid in avail] or ["  (none)"]
    lines += ["", f"PLAYER HISTORY: talk_counts={st.talk_counts} flags={[k for k, v in st.flags.items() if v]}"]
    return "\n".join(lines)


def _fallback(m: StoryMachine) -> dict:
    beat = m.current_beat
    return {
        "npc_goals": {npc: {"goal": goal, "hint": ""} for npc, goal in beat.npc_goals.items()},
        "quest_log": beat.quest_log,
        "side_surface": [],
    }


async def run_director(m: StoryMachine, reason: str) -> dict:
    """확정 비트 각색. 반환 {npc_goals: {npc: {goal, hint}}, quest_log, side_surface}. 실패 시 작가 원문."""
    if m.ended:
        return {"npc_goals": {}, "quest_log": "메인 퀘스트 완료", "side_surface": []}
    try:
        resp = await ollama_structured(
            DIRECTOR_SYSTEM_PROMPT,
            _build_user_prompt(m, reason),
            StoryDirectorResponse,
            model_name=STORY_MODEL,
            temperature=0.4,
            num_predict=400,
            timeout=DIRECTOR_TIMEOUT_S,
            log_extra={"stage": "story_director", "beat": m.state.main_beat},
        )
    except Exception as e:
        logger.warning(f"[Story] 디렉터 LLM 실패 — 작가 원문 사용 (beat={m.state.main_beat}): {e}")
        return _fallback(m)

    # 후보 밖 side id·미상 npc 는 버린다 — LLM 이 스토리 상태를 만지지 못하게.
    avail = set(m.available_sides())
    draft = m.current_beat.npc_goals
    goals: Dict[str, dict] = {}
    for g in resp.npc_goals:
        key = next((k for k in draft if k.casefold() == g.npc_id.strip().casefold()), None)
        if key and g.goal.strip():
            goals[key] = {"goal": g.goal.strip(), "hint": g.hint.strip()}
    for npc, goal in draft.items():  # LLM 이 빠뜨린 NPC 는 원문으로 보충
        goals.setdefault(npc, {"goal": goal, "hint": ""})
    dropped = [s for s in resp.side_surface if s not in avail]
    if dropped:
        logger.warning(f"[Story] 디렉터 side_surface 오염 제거: {dropped}")
    return {
        "npc_goals": goals,
        "quest_log": resp.quest_log.strip() or m.current_beat.quest_log,
        "side_surface": [s for s in resp.side_surface if s in avail],
    }
