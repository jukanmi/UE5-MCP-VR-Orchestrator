"""시나리오 NPC 콘텐츠를 런타임 위치로 심는다 — `python -m app.story.seed [--no-index]`.

personas/·knowledge/ 는 gitignore(런타임 데이터)라 시나리오 페르소나·지식의 원본은
app/story/content/npcs/ 에 두고, 이 스크립트가 복사 + FAISS 재빌드한다. 기존 페르소나·지식 .md 는 덮어쓴다(conversation_memory.json 은 보존).

  content/npcs/<npc>/persona.yaml       → app/agents/personas/generic/<npc>.yaml
  content/npcs/_world.md                → app/agents/knowledge/<npc>/lore/world.md   (전 NPC 공통)
  content/npcs/<npc>/{persona,history}.md → app/agents/knowledge/<npc>/{persona,history}/<npc>.md

서버 실행 중이면 재시작 필요 — load_persona 가 lru_cache 라 재시작 전엔 옛 페르소나가 남는다.
"""

import shutil
import sys
from pathlib import Path

from ..utils.build_knowledge import rebuild
from ..utils.rag_utils import KNOWLEDGE_BASE_PATH

NPCS_DIR = Path(__file__).parent / "content" / "npcs"
PERSONAS_DIR = Path("app/agents/personas/generic")


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
    seed(index="--no-index" not in sys.argv)
