"""fire-and-forget asyncio 태스크 공용 헬퍼 — main·dialogue 중복 패턴 단일화 (R4).

강한 참조 유지 필수: create_task 반환을 어디에도 보관하지 않으면 GC 가 실행 중
태스크를 수거해 무음 중단될 수 있다(asyncio 공식 문서 경고). 여기서 참조 보관과
완료 시 자동 해제·예외 로그를 일괄 처리한다.
"""

import asyncio
import logging
from typing import Any, Coroutine

logger = logging.getLogger(__name__)

_background_tasks: set = set()


def spawn_background(coro: Coroutine[Any, Any, Any], label: str = "") -> asyncio.Task:
    """fire-and-forget 태스크 등록 — 참조 유지 + 완료 시 해제 + 미처리 예외 로그.

    label 은 예외 로그 식별용(예: "victory-memory"). 예외를 삼키지 않고 로그로
    드러낸다 — 미보유 시 'Task exception was never retrieved' 무음 경고로만 남음.
    """
    task = asyncio.create_task(coro)
    _background_tasks.add(task)

    def _on_done(t: asyncio.Task) -> None:
        _background_tasks.discard(t)
        if not t.cancelled() and t.exception():
            logger.error(f"[AsyncTasks] 백그라운드 태스크 실패({label or 'unnamed'}): {t.exception()}")

    task.add_done_callback(_on_done)
    return task
