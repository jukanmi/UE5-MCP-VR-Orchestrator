"""
File: test_finetune_generators_smoke.py
Purpose: 학습 데이터 생성기가 서빙 DIALOGUE_STRUCTURED_PROMPT 와 어긋나면 즉시 실패하게 하는 스모크.

2026-09-18 `{story_goal}` 이 서빙 프롬프트에 추가됐을 때 두 생성기가 KeyError 로 죽었는데
서빙 테스트만 통과해 드러나지 않았다. LLM·Ollama 호출 없이 프롬프트 1건만 조립한다.
"""

import importlib.util
import os
import sys

import pytest

_ENGINE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
_SYNTH = os.path.join(_ENGINE, "finetune", "data", "synth")
sys.path.insert(0, _ENGINE)
sys.path.insert(0, _SYNTH)  # 생성기가 `from serving_prompt import ...` 로 형제 모듈을 읽는다

from serving_prompt import PROMPT_FIELDS, format_stage1_system  # noqa: E402

_PERSONA = {"name": "Guard", "role": "성문 경비", "traits": ["과묵"], "speech_style": None}


def _load(name: str):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_SYNTH, f"{name}.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.mark.parametrize("gen", ["generate_stage1", "generate_expanded_stage1"])
def test_generator_build_system_fills_every_serving_placeholder(gen):
    """서빙 프롬프트에 placeholder 가 추가되면 여기서 KeyError 로 깨진다."""
    system = _load(gen).build_system(_PERSONA, ["Player", "Self"], "None (empty-handed)")
    assert "Guard" in system
    assert "right now): None" in system  # story_goal 줄이 서빙 기본값 "None" 으로 채워짐


def test_format_stage1_system_rejects_missing_and_extra_fields():
    given = {f: "x" for f in PROMPT_FIELDS}
    assert format_stage1_system(**given)
    with pytest.raises(KeyError):
        format_stage1_system(**{k: v for k, v in given.items() if k != "name"})
    with pytest.raises(KeyError):
        format_stage1_system(**given, bogus="x")
