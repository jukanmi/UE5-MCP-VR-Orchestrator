"""DB 에만 있는 호감도 행이 기동 시 캐시로 올라와 state_update 응답(relations)에 실리는지.

배경: get_relations_from_cache 는 캐시만 보고, 캐시는 get_affinity 가 짝 단위로 lazy 적재한다.
seed/디버그로 DB 에 직접 쓴 보스↔아군 Hostile 이 C++ 에 영영 안 실리던 문제(2026-09-18 PIE 실측).
"""

import asyncio
import sqlite3
from unittest.mock import patch

from app.utils import db_manager


def test_warm_cache_loads_db_rows_into_relations(tmp_path):
    db = tmp_path / "affinity.db"
    with patch.object(db_manager, "DB_PATH", str(db)), patch.object(db_manager, "_affinity_cache", {}):
        asyncio.run(db_manager.init_db())  # 빈 DB → 0행
        assert db_manager.get_relations_from_cache("Commander_Vorg") == []

        con = sqlite3.connect(db)
        con.execute(
            "INSERT INTO npc_relations (source_id, target_id, affinity_score, reputation_tag, last_interaction) "
            "VALUES ('Commander_Vorg', 'Skadi', -100, 'Hostile', 'story_seed')"
        )
        con.commit()
        con.close()

        assert asyncio.run(db_manager.warm_cache()) == 1
        rels = db_manager.get_relations_from_cache("Commander_Vorg")
        assert rels == [{"target_id": "Skadi", "affinity_score": -100, "reputation_tag": "Hostile"}]
        assert asyncio.run(db_manager.warm_cache()) == 0  # 이미 캐시된 짝은 덮지 않음
