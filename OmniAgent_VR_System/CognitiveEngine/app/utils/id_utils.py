"""대소문자 무시 NPC/Actor ID 조회 헬퍼.

WHY: C++ NPCMap 은 케이스 구분하나 LLM·디버그 입력은 케이스가 흔들림.
"lower 로 찾고 원본 케이스 반환" 패턴이 dialogue/interface_input 여러 곳에
중복 → 단일화. (P1 `_vr_get` 과 같은 성격의 공용 헬퍼)
"""

from typing import Iterable, Mapping, Optional, TypeVar

_V = TypeVar("_V")


def ci_id_map(ids: Iterable[str]) -> dict[str, str]:
    """['Elara', 'James'] → {'elara': 'Elara', 'james': 'James'}. 원본 케이스 보존용 lower 인덱스."""
    return {i.lower(): i for i in ids}


def ci_get(mapping: Mapping[str, _V], key: str, default: Optional[_V] = None) -> Optional[_V]:
    """key 를 대소문자 무시로 조회. 첫 매칭 값 반환, 없거나 key 빈 값이면 default."""
    if not key:
        return default
    key_lower = key.lower()
    for k, v in mapping.items():
        if k.lower() == key_lower:
            return v
    return default
