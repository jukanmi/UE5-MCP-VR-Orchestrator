# -*- coding: utf-8 -*-
"""학습 데이터 생성기 공용 — 서빙 DIALOGUE_STRUCTURED_PROMPT 조립 단일 진입점.

placeholder 목록은 서빙 프롬프트 문자열에서 직접 읽는다(복붙 금지). 서빙 쪽에 placeholder 가
늘거나 줄면 이 함수가 즉시 KeyError 로 실패하고, tests/test_finetune_generators_smoke.py 가 이를 잡는다.
"""
import os
import sys
from string import Formatter

_ENGINE_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))
if _ENGINE_ROOT not in sys.path:
    sys.path.insert(0, _ENGINE_ROOT)

from app.agents.subgraphs.prompts import DIALOGUE_STRUCTURED_PROMPT  # noqa: E402

# 서빙에는 기본값 상수가 없고 app/agents/subgraphs/dialogue.py 의 _collect_stage1_context 에 리터럴로 박혀 있다
# (memory_summary "None" / story_goal `directive.get("goal") or "None"` / rag_context `... else "None"`).
# 그쪽 값을 바꾸면 여기도 맞출 것. 학습 행은 세션 상태가 없는 중립 상태.
SERVING_DEFAULTS = {"memory": "None", "story_goal": "None", "rag_context": "None"}

PROMPT_FIELDS = frozenset(f for _, f, _, _ in Formatter().parse(DIALOGUE_STRUCTURED_PROMPT) if f)


def format_stage1_system(**given: str) -> str:
    """서빙 프롬프트 placeholder 를 given + SERVING_DEFAULTS 로 채운다. 누락·초과 필드는 KeyError."""
    kwargs = {**SERVING_DEFAULTS, **given}
    missing = PROMPT_FIELDS - kwargs.keys()
    extra = kwargs.keys() - PROMPT_FIELDS
    if missing or extra:
        raise KeyError(f"서빙 프롬프트와 불일치 — 누락 {sorted(missing)}, 초과 {sorted(extra)}")
    return DIALOGUE_STRUCTURED_PROMPT.format(**kwargs)
