"""시나리오 NPC 콘텐츠를 런타임 위치로 심는다 — `python -m app.story.seed [--no-index] [--affinity-only]`.

personas/·knowledge/ 는 gitignore(런타임 데이터)라 시나리오 페르소나·지식의 원본은
app/story/content/npcs/ 에 두고, 이 스크립트가 복사 + FAISS 재빌드한다. 기존 페르소나·지식 .md 는 덮어쓴다(conversation_memory.json 은 보존).

  content/npcs/<npc>/persona.yaml       → app/agents/personas/generic/<npc>.yaml
  content/npcs/_world.md                → app/agents/knowledge/<npc>/lore/world.md   (전 NPC 공통)
  content/npcs/<npc>/{persona,history}.md → app/agents/knowledge/<npc>/{persona,history}/<npc>.md

호감도도 시딩한다: 보스↔플레이어·보스↔아군 = -100(Hostile, 시야만으로 교전), 아군→플레이어 = 20(중립 —
과거 PIE 피격으로 남은 Hostile 행 초기화). C++ 는 affinity ≤ -30 을 Hostile 로 본다.

서버 실행 중이면 재시작 필요 — load_persona 가 lru_cache 고 affinity 캐시도 프로세스 안에 있다.
"""

import asyncio
import shutil
import sys
from pathlib import Path

from ..utils import db_manager
from ..utils.build_knowledge import rebuild
from ..utils.rag_utils import KNOWLEDGE_BASE_PATH

NPCS_DIR = Path(__file__).parent / "content" / "npcs"
PERSONAS_DIR = Path("app/agents/personas/generic")

# perception·대화가 플레이어 id 로 폰 액터 이름을 쓴다(VRPawn::GetName). 폰 클래스가 바뀌면 여기도.
# ponytail: 플레이어 id 를 "Player" 상수로 통일하는 게 정답 — C++ 3곳(VRPawn·HUD·PerceptionIdFor) 동시 수정 필요.
PLAYER_KEY = "BP_VRPawn_C_0"
ALLIES = ["Elara", "James", "Skadi", "Moca", "Guard"]
BOSSES = ["Commander_Vorg", "DemonLord"]  # main.yaml boss_id 와 일치
# 포로 연출: Elara 는 전초기지 우리 안에 갇혀 있고 Vorg 는 그녀를 방치한다. Hostile 로 두면 레벨 시작 즉시
# 우리 안에서 교전이 붙어 플레이어 도착 전에 결판난다(시야 30m 안). 중립(0) = 시야 danger 0.3 → 무교전.
CAPTIVE_PAIRS = {("Commander_Vorg", "Elara")}


async def seed_affinity() -> None:
    await db_manager.init_db()
    for boss in BOSSES:
        await db_manager.set_affinity_direct(boss, PLAYER_KEY, -100, "story_seed")
        for ally in ALLIES:
            score = 0 if (boss, ally) in CAPTIVE_PAIRS else -100
            await db_manager.set_affinity_direct(boss, ally, score, "story_seed")
            await db_manager.set_affinity_direct(ally, boss, score, "story_seed")
    for ally in ALLIES:
        await db_manager.set_affinity_direct(ally, PLAYER_KEY, 20, "story_seed")
    print(f"[Seed] affinity: 보스 {BOSSES} ↔ 플레이어/아군 Hostile, 아군→플레이어 20")


def seed(index: bool = True) -> list[str]:
    """복사 후 재빌드한 NPC id(소문자) 목록 반환. CognitiveEngine 디렉토리에서 실행 전제(경로 상대)."""
    world = NPCS_DIR / "_world.md"
    done = []
    for npc_dir in sorted(p for p in NPCS_DIR.iterdir() if p.is_dir()):
        npc = npc_dir.name
        PERSONAS_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(npc_dir / "persona.yaml", PERSONAS_DIR / f"{npc}.yaml")

        kb = Path(KNOWLEDGE_BASE_PATH) / npc
        for cat in ("lore", "persona", "history"):
            (kb / cat).mkdir(parents=True, exist_ok=True)
            for old in (kb / cat).glob("*.md"):  # 이전 세계관 .md 잔존 시 RAG 가 섞인다
                old.unlink()
        shutil.copyfile(world, kb / "lore" / "world.md")
        for cat in ("persona", "history"):
            src = npc_dir / f"{cat}.md"
            if src.exists():
                shutil.copyfile(src, kb / cat / f"{npc}.md")
        print(f"[Seed] {npc}: persona.yaml + knowledge 복사")
        if index:
            rebuild(npc, force=True)
        done.append(npc)
    return done


if __name__ == "__main__":
    if "--affinity-only" not in sys.argv:
        seed(index="--no-index" not in sys.argv)
    asyncio.run(seed_affinity())
