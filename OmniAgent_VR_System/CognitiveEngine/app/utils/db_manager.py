import aiosqlite
import asyncio
import logging
import threading
from typing import Dict, Tuple, Optional
from ..schemas.vr_context import NPCRelation
import os

logger = logging.getLogger(__name__)

# --- Configuration ---
DB_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "affinity.db")
SYNC_INTERVAL_SECONDS = 5.0

# 캐시·직접 설정 두 경로가 같은 UPSERT 를 쓴다.
_UPSERT_RELATION_SQL = """
    INSERT INTO npc_relations (source_id, target_id, affinity_score, reputation_tag, last_interaction)
    VALUES (?, ?, ?, ?, ?)
    ON CONFLICT(source_id, target_id)
    DO UPDATE SET
        affinity_score=excluded.affinity_score,
        reputation_tag=excluded.reputation_tag,
        last_interaction=excluded.last_interaction,
        updated_at=CURRENT_TIMESTAMP
"""


def reputation_tag_for(score: int) -> str:
    """호감도 → 관계 태그. 임계 ±30 은 C++ NPCStateComponent::GetRelation 과 같은 값."""
    if score <= -30:
        return "Hostile"
    if score >= 30:
        return "Friendly"
    return "Neutral"


# --- State ---
# Memory Cache: (source_id, target_id) -> NPCRelation
_affinity_cache: Dict[Tuple[str, str], NPCRelation] = {}
# 캐시는 async 이벤트루프와 동기 스레드풀(to_thread)에서 동시 접근 가능 — dict 변경 중 순회
# RuntimeError 방지용 락. 짧은 dict 연산에만 보유(await 가로질러 보유 금지).
_cache_lock = threading.Lock()
_sync_task: Optional[asyncio.Task] = None
_is_shutting_down: bool = False


async def init_db():
    """DB 디렉토리 및 테이블 생성 (최초 1회 실행)"""
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    try:
        async with aiosqlite.connect(DB_PATH) as db:
            await db.execute("""
                CREATE TABLE IF NOT EXISTS npc_relations (
                    source_id TEXT,
                    target_id TEXT,
                    affinity_score INTEGER DEFAULT 0,
                    reputation_tag TEXT DEFAULT 'Neutral',
                    last_interaction TEXT,
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    PRIMARY KEY (source_id, target_id)
                )
            """)
            await db.commit()
            logger.info(f"[DBManager] 데이터베이스 초기화 성공: {DB_PATH}")
    except Exception as e:
        logger.error(f"[DBManager] 데이터베이스 초기화 실패: {e}")
        raise


async def start_background_sync():
    """백그라운드 동기화 태스크 시작"""
    global _sync_task
    if _sync_task is None:
        _sync_task = asyncio.create_task(_background_sync_loop())
        logger.info("[DBManager] 백그라운드 캐시 동기화 태스크 시작됨.")


async def stop_background_sync():
    """시스템 종료 시 남은 캐시를 플러시하고 태스크 종료"""
    global _is_shutting_down, _sync_task
    logger.info("[DBManager] 시스템 종료: 남아있는 더티 캐시를 강제 동기화합니다...")
    _is_shutting_down = True

    if _sync_task:
        # 진행 중인 루프 한 번만 돌리고 취소
        await _flush_dirty_cache()
        _sync_task.cancel()
        try:
            await _sync_task
        except asyncio.CancelledError:
            pass
        logger.info("[DBManager] 백그라운드 캐시 동기화 태스크 종료 및 플러시 완료.")


async def get_affinity(source_id: str, target_id: str) -> NPCRelation:
    """메모리 캐시 조회 -> 없으면 DB에서 로드 후 캐싱"""
    key = (source_id, target_id)
    with _cache_lock:
        cached = _affinity_cache.get(key)
    if cached is not None:
        return cached

    # Cache Miss -> DB 조회
    try:
        async with aiosqlite.connect(DB_PATH) as db:
            db.row_factory = aiosqlite.Row
            cursor = await db.execute(
                "SELECT * FROM npc_relations WHERE source_id=? AND target_id=?", (source_id, target_id)
            )
            row = await cursor.fetchone()

            if row:
                relation = NPCRelation(
                    source_id=row["source_id"],
                    target_id=row["target_id"],
                    affinity_score=row["affinity_score"],
                    reputation_tag=row["reputation_tag"],
                    last_interaction=row["last_interaction"],
                    is_dirty=False,
                )
            else:
                # DB에도 없으면 기본 0짜리 신규 생성
                relation = NPCRelation(source_id=source_id, target_id=target_id, is_dirty=True)

            with _cache_lock:
                _affinity_cache[key] = relation
            return relation
    except Exception as e:
        logger.error(f"[DBManager] Cache miss 중 DB 로드 에러 (soruce={source_id}): {e}")
        # 에러 시 임시 기본값 반환
        return NPCRelation(source_id=source_id, target_id=target_id)


def update_affinity_sync(source_id: str, target_id: str, score_delta: int, interaction_summary: str = ""):
    """
    메모리 캐시 즉각 반영 (동기 함수).
    FastAPI의 라우트나 다른 컴포넌트에서 await 없이 호출 가능
    초회 접근 시에는 데이터가 없을 수 있으므로 이 함수를 쓰기 전에 get_affinity를 먼저 호출했음을 가정함.
    """
    key = (source_id, target_id)
    # 조회~가감~태그 재계산을 한 락 안에서 처리 — 락을 중간에 놓으면 같은 쌍에 대한
    # 동시 delta 가 서로를 덮어써 read-modify-write lost update 가 발생한다.
    with _cache_lock:
        relation = _affinity_cache.get(key)
        if relation is None:
            logger.warning(
                f"[DBManager] update_affinity_sync: 캐시에 존재하지 않는 대상. get_affinity를 선행 호출하세요. {key}"
            )
            # 일단 0에서 가감해서 밀어넣음
            relation = NPCRelation(source_id=source_id, target_id=target_id)
            _affinity_cache[key] = relation

        # 스코어 클램핑 (-100 ~ 100)
        new_score = relation.affinity_score + score_delta
        relation.affinity_score = max(-100, min(100, new_score))

        # pydantic v2 는 할당 시 validation 을 돌리지 않으므로 태그를 직접 재계산.
        relation.reputation_tag = reputation_tag_for(relation.affinity_score)

        relation.last_interaction = interaction_summary
        relation.is_dirty = True
    logger.debug(f"[DBManager] 캐시 업데이트 (Dirty Mark): {key} -> Score: {relation.affinity_score}")


def get_relations_from_cache(source_id: str) -> list:
    """캐시에 있는 source_id의 모든 관계를 동기적으로 반환 (state_update 응답용)."""
    # 순회 중 to_thread 워커가 키를 추가하면 RuntimeError: dictionary changed size
    # during iteration — _flush_dirty_cache 와 동일하게 락 하에 스냅샷을 뜬다.
    with _cache_lock:
        snapshot = [rel for (src, _), rel in _affinity_cache.items() if src == source_id]
    return [
        {
            "target_id": rel.target_id,
            "affinity_score": rel.affinity_score,
            "reputation_tag": rel.reputation_tag,
        }
        for rel in snapshot
    ]


async def set_affinity_direct(
    source_id: str, target_id: str, score: int, interaction_summary: str = "debug_override"
) -> NPCRelation:
    """호감도를 절대값으로 직접 설정 (디버그 대시보드용). 캐시와 DB 동시 갱신."""
    score = max(-100, min(100, score))
    tag = reputation_tag_for(score)

    relation = NPCRelation(
        source_id=source_id,
        target_id=target_id,
        affinity_score=score,
        reputation_tag=tag,
        last_interaction=interaction_summary,
        is_dirty=False,
    )
    with _cache_lock:
        _affinity_cache[(source_id, target_id)] = relation

    try:
        async with aiosqlite.connect(DB_PATH) as db:
            await db.execute(_UPSERT_RELATION_SQL, (source_id, target_id, score, tag, interaction_summary))
            await db.commit()
    except Exception as e:
        logger.error(f"[DBManager] set_affinity_direct 실패: {e}")

    return relation


async def delete_affinity(source_id: str, target_id: str) -> None:
    """호감도 레코드 삭제 (캐시 + DB)."""
    with _cache_lock:
        _affinity_cache.pop((source_id, target_id), None)
    try:
        async with aiosqlite.connect(DB_PATH) as db:
            await db.execute("DELETE FROM npc_relations WHERE source_id=? AND target_id=?", (source_id, target_id))
            await db.commit()
    except Exception as e:
        logger.error(f"[DBManager] delete_affinity 실패: {e}")


async def get_all_affinity() -> list[dict]:
    """DB에 저장된 모든 호감도 레코드를 반환 (디버그 대시보드용)."""
    # 먼저 캐시의 dirty 항목을 플러시
    await _flush_dirty_cache()
    try:
        async with aiosqlite.connect(DB_PATH) as db:
            db.row_factory = aiosqlite.Row
            cursor = await db.execute(
                "SELECT source_id, target_id, affinity_score, reputation_tag, last_interaction, updated_at "
                "FROM npc_relations ORDER BY updated_at DESC"
            )
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]
    except Exception as e:
        logger.error(f"[DBManager] get_all_affinity 실패: {e}")
        return []


async def _background_sync_loop():
    """주기적으로 변경사항을 DB에 쓰는 타이머 루프"""
    while not _is_shutting_down:
        await asyncio.sleep(SYNC_INTERVAL_SECONDS)
        await _flush_dirty_cache()


async def _flush_dirty_cache():
    """is_dirty=True 인 모든 캐시들을 모아 트랜잭션 단위로 일괄 저장"""
    # 순회 중 다른 스레드의 키 추가로 인한 RuntimeError 방지 — 락 하에 스냅샷.
    with _cache_lock:
        dirty_items = [rel for rel in _affinity_cache.values() if rel.is_dirty]
    if not dirty_items:
        return

    # 실행용 튜플 배열 만들기
    data_to_write = [
        (r.source_id, r.target_id, r.affinity_score, r.reputation_tag, r.last_interaction) for r in dirty_items
    ]

    try:
        async with aiosqlite.connect(DB_PATH) as db:
            await db.execute("BEGIN TRANSACTION")
            await db.executemany(_UPSERT_RELATION_SQL, data_to_write)
            await db.commit()

            # DB 커밋 완전히 성공한 뒤에 메모리의 dirty 마크 제거 (원자성 확보)
            for item in dirty_items:
                item.is_dirty = False
            logger.info(f"[DBManager] 성공적으로 {len(dirty_items)}건의 관계 데이터 동기화 완료.")

    except Exception as e:
        logger.error(f"[DBManager] 일괄 데이터 동기화 실패. Rollback 수행됨: {e}")
        # 오류 발생 시 is_dirty는 True로 남아서 다음 턴에 재시도 됨.
