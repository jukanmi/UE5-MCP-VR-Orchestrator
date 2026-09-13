# -*- coding: utf-8 -*-
"""파인튜닝용 구조화 로그 — LLM 호출·Rules 판정을 jsonl 로 영속화.

목적: 실플레이 (프롬프트, 구조화 응답, 사후 검증 결과) 를 학습 데이터로 축적
(SPEC_finetune M1/M4). msg_id 로 LLM 레코드와 Rules 레코드를 오프라인 조인.

쓰기 경로: app/data/train_logs/YYYY-MM-DD.jsonl (날짜별 파일 = 회전 단위, gitignore).
핫패스 보호: 호출측이 spawn_background(asyncio.to_thread(...)) 로 fire-and-forget —
여기 함수들은 동기 append 만 담당 (memory_manager._save_to_file 과 동일 관례).
"""
import logging
import json
import threading
from datetime import datetime, timezone
from pathlib import Path


logger = logging.getLogger(__name__)
LOG_DIR = Path(__file__).parent.parent / "data" / "train_logs"

_lock = threading.Lock()


def _append(record: dict) -> None:
    """날짜별 jsonl append. 실패는 조용히 무시 — 로깅이 파이프라인을 깨면 안 됨."""
    try:
        now = datetime.now(timezone.utc)
        record["ts"] = now.isoformat(timespec="milliseconds")
        path = LOG_DIR / f"{now:%Y-%m-%d}.jsonl"
        line = json.dumps(record, ensure_ascii=False, default=str)
        with _lock:
            LOG_DIR.mkdir(parents=True, exist_ok=True)
            with open(path, "a", encoding="utf-8") as fp:
                fp.write(line + "\n")
    except Exception as e:  # noqa: BLE001
        logger.warning(f"[TrainLogger] 기록 실패(무시): {e}")


def log_llm_call(
    *,
    stage: str,
    model_id: str,
    system_prompt: str,
    user_prompt: str,
    raw_response: str = "",
    parsed: dict | None = None,
    error: str = "",
    elapsed_ms: float = 0.0,
    temperature: float = 0.0,
    extra: dict | None = None,
) -> None:
    """LLM 호출 1건 기록. extra: 호출부 컨텍스트(msg_id/npc_id/valid_targets/attempt 등)."""
    _append({
        "kind": "llm",
        "stage": stage,
        "model_id": model_id,
        "temperature": temperature,
        "elapsed_ms": round(elapsed_ms, 1),
        "system_prompt": system_prompt,
        "user_prompt": user_prompt,
        "raw_response": raw_response,
        "parsed_ok": parsed is not None,
        "parsed": parsed or {},
        "error": error,
        **(extra or {}),
    })


def log_rules_result(
    *,
    npc_id: str,
    actions_before: int,
    actions_after: int,
    corrections: list[str],
    mode_before: str,
    mode_after: str,
    extra: dict | None = None,
) -> None:
    """Rules 검증 1배치 기록 — LLM 레코드와 msg_id(extra) 로 조인."""
    _append({
        "kind": "rules",
        "npc_id": npc_id,
        "actions_before": actions_before,
        "actions_after": actions_after,
        "passed": actions_after > 0 or actions_before == 0,
        "corrections": corrections,
        "mode_before": mode_before,
        "mode_after": mode_after,
        **(extra or {}),
    })
