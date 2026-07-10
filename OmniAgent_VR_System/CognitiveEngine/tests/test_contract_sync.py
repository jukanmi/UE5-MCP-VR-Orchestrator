"""UE5(C++) ↔ Python 계약 동기화 검사 — CLAUDE.md §3(EAction)·§5(Envelope) 자동화.

C++ 빌드 없이 소스 텍스트 파싱으로 양쪽 enum/키를 비교한다.
한쪽만 수정하면 "메시지 무음 무시"(§5)·"액션 무음 no-op"(§3)이 되므로
여기서 diff 를 즉시 드러내는 것이 목적. 소스 파일 경로가 깨지면 skip 이 아니라
실패한다(경로 이동도 계약 위반 신호).
"""

import re
from pathlib import Path
from typing import get_args

import pytest

from app.schemas.actions import (
    ACTION_REQUIRED_PARAMS,
    CATEGORY_ACTION_MAP,
    DIALOGUE_ACTION_FIELD_MAP,
    DialogueActionItem,
    EAction,
    NPCBehaviorMode,
    NPCFacialState,
)
from app.schemas.envelope import EEnvelopeType

# tests/ → CognitiveEngine → OmniAgent_VR_System → 레포 루트
REPO_ROOT = Path(__file__).resolve().parents[3]
ACTION_TYPES_H = REPO_ROOT / "Source/UE5_MCP_VR/NPC/Struct/NPCActionTypes.h"
ACTION_KEYS_H = REPO_ROOT / "Source/UE5_MCP_VR/NPC/Struct/NPCActionKeys.h"
ACTION_COMPONENT_CPP = REPO_ROOT / "Source/UE5_MCP_VR/NPC/Action/NPCActionComponent.cpp"
ENVELOPE_BUILDER_H = REPO_ROOT / "Source/UE5_MCP_VR/Network/EnvelopeBuilder.h"
ENVELOPE_BUILDER_CPP = REPO_ROOT / "Source/UE5_MCP_VR/Network/EnvelopeBuilder.cpp"
MAIN_PY = REPO_ROOT / "OmniAgent_VR_System/CognitiveEngine/app/main.py"


def _read(path: Path) -> str:
    if not path.exists():
        pytest.fail(f"계약 소스 파일 없음(이동/삭제 시 이 테스트 경로도 갱신 필요): {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def _cpp_enum_block(header_text: str, enum_name: str) -> str:
    """`enum class <name>` 선언부터 닫는 `};` 까지 잘라 반환."""
    m = re.search(rf"enum\s+class\s+{enum_name}\b.*?\{{(.*?)\}};", header_text, re.DOTALL)
    assert m, f"C++ 헤더에서 enum class {enum_name} 을 찾지 못함"
    return m.group(1)


def _umeta_enum_entries(header_text: str, enum_name: str) -> set:
    """UMETA 표기 enum 의 항목 이름 집합 (EAction/EFacialState/ENPCBehaviorMode 형)."""
    block = _cpp_enum_block(header_text, enum_name)
    entries = set(re.findall(r"^\s*(\w+)\s+UMETA", block, re.MULTILINE))
    assert entries, f"{enum_name} 블록에서 UMETA 항목 파싱 실패(형식 변경?)"
    return entries


# ─────────────────────────────────────────────────────────────────────────────
# §3 EAction — NPCActionTypes.h ↔ schemas/actions.py
# ─────────────────────────────────────────────────────────────────────────────


def test_eaction_enum_matches_cpp():
    cpp = _umeta_enum_entries(_read(ACTION_TYPES_H), "EAction")
    py = set(get_args(EAction))
    assert cpp == py, (
        f"EAction 불일치 — C++ 에만: {sorted(cpp - py)} / Python 에만: {sorted(py - cpp)}. "
        "§3: 새 EAction 은 NPCActionTypes.h·actions.py 양쪽 동시 수정(/add-eaction)."
    )


def test_behavior_mode_matches_cpp():
    cpp = _umeta_enum_entries(_read(ACTION_TYPES_H), "ENPCBehaviorMode")
    py = set(get_args(NPCBehaviorMode))
    assert cpp == py, f"BehaviorMode 불일치 — C++ 에만: {sorted(cpp - py)} / Python 에만: {sorted(py - cpp)}"


def test_facial_state_matches_cpp():
    cpp = _umeta_enum_entries(_read(ACTION_TYPES_H), "EFacialState")
    py = set(get_args(NPCFacialState))
    assert cpp == py, f"FacialState 불일치 — C++ 에만: {sorted(cpp - py)} / Python 에만: {sorted(py - cpp)}"


def test_category_map_covers_all_actions():
    """CATEGORY_ACTION_MAP 합집합 == EAction — 카테고리 미배정 액션은 Rules Mode 보정 누락."""
    mapped = set().union(*CATEGORY_ACTION_MAP.values())
    py = set(get_args(EAction))
    assert mapped == py, f"카테고리 미배정: {sorted(py - mapped)} / EAction 에 없는 항목: {sorted(mapped - py)}"


def test_required_params_only_known_actions():
    unknown = set(ACTION_REQUIRED_PARAMS) - set(get_args(EAction))
    assert not unknown, f"ACTION_REQUIRED_PARAMS 에 EAction 아닌 키: {sorted(unknown)}"


def test_param_keys_exist_in_cpp():
    """Python 이 송신하는 Parameters 키가 C++ NPCActionKeys 에 동일 철자로 존재하는지."""
    header = _read(ACTION_KEYS_H)
    py_keys = {param_key for param_key, _field in DIALOGUE_ACTION_FIELD_MAP}
    py_keys |= {"text"}  # Dialogue 필수 파라미터 (ACTION_REQUIRED_PARAMS)
    missing = {k for k in py_keys if f'TEXT("{k}")' not in header}
    assert not missing, f"NPCActionKeys.h 에 없는 Python 송신 키: {sorted(missing)} (§1 — 리터럴/철자 확인)"


def test_move_style_vocabulary_matches_cpp():
    """Parameters['style'] 이동 어휘 = EMoveType 전 항목.

    ParseMoveStyle 이 미매칭 값을 Walk 로 폴백하므로, EMoveType 에 항목을 추가하고
    ParseMoveStyle 분기를 빠뜨리면 해당 스타일이 조용히 Walk 로 뭉개진다.
    Python 스키마(style Field description)도 같은 어휘를 LLM 에 노출해야 한다.
    """
    enum_block = _cpp_enum_block(_read(ACTION_TYPES_H), "EMoveType")
    move_types = {m.group(1) for m in re.finditer(r"^\s*(\w+)\s*,?\s*$", enum_block, re.MULTILINE)}
    assert move_types, "EMoveType 항목 파싱 실패(형식 변경?)"

    parse_fn = re.search(
        r"EMoveType\s+UNPCActionComponent::ParseMoveStyle.*?\n\}", _read(ACTION_COMPONENT_CPP), re.DOTALL
    )
    assert parse_fn, "NPCActionComponent.cpp 에서 ParseMoveStyle 정의를 찾지 못함"
    handled = set(re.findall(r'TEXT\("(\w+)"\)', parse_fn.group(0)))
    assert move_types == handled, (
        f"ParseMoveStyle 미처리 EMoveType: {sorted(move_types - handled)} / "
        f"enum 에 없는 분기: {sorted(handled - move_types)}"
    )

    style_desc = DialogueActionItem.model_fields["style"].description or ""
    missing_in_py = {t for t in move_types if t not in style_desc}
    assert not missing_in_py, (
        f"actions.py style description 에 누락된 이동 어휘: {sorted(missing_in_py)} — "
        "LLM 이 해당 스타일을 생성할 수 없다."
    )


# ─────────────────────────────────────────────────────────────────────────────
# §5 Envelope — EnvelopeBuilder ↔ schemas/envelope.py ↔ main.py 수신 분기
# ─────────────────────────────────────────────────────────────────────────────


def _cpp_wire_map() -> dict:
    """EnvelopeTypeToString switch 에서 (C++ enum 멤버 → wire 문자열) 추출."""
    cpp = _read(ENVELOPE_BUILDER_CPP)
    pairs = re.findall(r'case\s+EEnvelopeType::(\w+):\s*return\s+TEXT\("(\w+)"\)', cpp)
    assert pairs, "EnvelopeTypeToString switch 파싱 실패(형식 변경?)"
    return dict(pairs)


def test_cpp_envelope_enum_fully_switched():
    """C++ enum 멤버 전원이 EnvelopeTypeToString switch 에 있는지 — 누락 시 'prompt' 폴백(무음 오라우팅)."""
    header = _read(ENVELOPE_BUILDER_H)
    block = _cpp_enum_block(header, "EEnvelopeType")
    members = set(re.findall(r"^\s*(\w+),", block, re.MULTILINE))
    assert members, "C++ EEnvelopeType 멤버 파싱 실패"
    switched = set(_cpp_wire_map())
    assert members == switched, (
        f"switch 미반영 멤버: {sorted(members - switched)} / enum 에 없는 case: {sorted(switched - members)}"
    )


def test_envelope_wire_strings_match_python():
    cpp_wires = set(_cpp_wire_map().values())
    py_wires = {t.value for t in EEnvelopeType}
    assert cpp_wires == py_wires, (
        f"Envelope wire 불일치 — C++ 에만: {sorted(cpp_wires - py_wires)} / Python 에만: {sorted(py_wires - cpp_wires)}. "
        "§5: EnvelopeBuilder·envelope.py·수신 분기 동시 수정(/add-envelope)."
    )


def test_all_envelope_types_dispatched_in_main():
    """envelope.py 에 타입 추가 후 main.py 수신 분기 누락 시 메시지 무음 무시 — §5 핵심 함정."""
    main_src = _read(MAIN_PY)
    missing = [t.name for t in EEnvelopeType if f"EEnvelopeType.{t.name}" not in main_src]
    assert not missing, f"main.py 수신 분기에 없는 EEnvelopeType: {missing} (§5 — /add-envelope 체크리스트)"
