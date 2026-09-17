"""스토리 디렉터 진입점 — 결정론 상태기계(state.py) + LLM 각색(director.py).

get_story() 가 None 이면 전 훅 no-op: STORY_ENABLED=0 이거나 content/main.yaml 부재.
Stage2 와 별도 모듈 — 주기(비트 전이 시만)·입력(비트 시트)·출력(goal/quest_log)이 전부 다르다.
"""

import logging
import os
from pathlib import Path
from typing import Optional

from .director import run_director
from .loader import load_content
from .state import StoryMachine

logger = logging.getLogger(__name__)

CONTENT_DIR = Path(os.environ.get("STORY_CONTENT_DIR", Path(__file__).parent / "content"))
STATE_PATH = Path(os.environ.get("STORY_STATE_PATH", Path(__file__).parent / "story_state.json"))

_machine: Optional[StoryMachine] = None
_loaded = False


def get_story() -> Optional[StoryMachine]:
    """지연 로드 싱글턴. 로더 검증 실패는 ValueError 로 전파(기동 실패가 목적)."""
    global _machine, _loaded
    if _loaded:
        return _machine
    _loaded = True
    if os.environ.get("STORY_ENABLED", "1") == "0" or not (CONTENT_DIR / "main.yaml").exists():
        logger.info("[Story] 비활성 (STORY_ENABLED=0 또는 main.yaml 없음)")
        return None
    _machine = StoryMachine(load_content(CONTENT_DIR), STATE_PATH)
    logger.info(f"[Story] 로드: '{_machine.content.main.title}' beat={_machine.state.main_beat}")
    return _machine


def set_story(machine: Optional[StoryMachine]) -> None:
    """테스트 주입용."""
    global _machine, _loaded
    _machine, _loaded = machine, True


async def direct_if_needed(m: StoryMachine, reason: str) -> None:
    """dirty(전이·해금·첫 기동)일 때만 디렉터 LLM 호출 → 캐시. 아니면 0회."""
    if not m.needs_direction:
        return
    await m.apply_direction(await run_director(m, reason))
