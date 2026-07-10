"""
File: test_rules_validation.py
Purpose: Rules Agent의 타겟 ID 검증 및 좌표 범위 검증 유닛 테스트.

검증 항목:
  - 유효하지 않은 target_id → 해당 액션 제거
  - WORLD_BOUNDS 밖 좌표 → 해당 액션 제거
  - 유효한 타겟 + 정상 좌표 → 통과
  - 수치 파라미터 클램핑 (damage, speed)

NOTE: 현재 Rules 계약은 GameAction(ActionType, Parameters) 기반 — target_id/target_loc 는
      Parameters dict 안의 문자열 항목이다(구 NPCAction/GameVector3 별도 필드 모델 폐기).
      또한 ACTION_REQUIRED_PARAMS 로 필수 파라미터 누락 액션은 검증 초입에서 제거되므로,
      각 케이스는 대상 액션의 필수 파라미터를 반드시 채워야 한다(Attack→target_id, Dialogue→text).
"""

import sys
import os

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from app.agents.subgraphs.rules import (
    _is_target_id_valid,
    _is_target_loc_in_bounds,
    validate_and_clamp_action,
)
from app.schemas.actions import GameAction


def _loc(x: float, y: float, z: float) -> str:
    """target_loc UE 문자열 — _is_target_loc_in_bounds 가 파싱하는 'X=.. Y=.. Z=..' 형식."""
    return f"X={x} Y={y} Z={z}"


def make_action(
    action_type: str = "Dialogue",
    target_id: str | None = None,
    target_loc: str | None = None,
    params: dict | None = None,
) -> GameAction:
    """테스트용 GameAction 헬퍼 — target_id/target_loc 를 Parameters dict 로 접어 넣는다."""
    p: dict = dict(params or {})
    if target_id is not None:
        p["target_id"] = target_id
    if target_loc is not None:
        p["target_loc"] = target_loc
    return GameAction(ActionType=action_type, Parameters=p)


class TestTargetIdValidation:
    """타겟 ID 유효성 검증 테스트."""

    def test_valid_npc_id(self):
        action, corrections = validate_and_clamp_action(
            make_action(action_type="Dialogue", target_id="Elara", params={"text": "안녕"})
        )
        assert action is not None, "유효한 NPC ID는 통과해야 함"
        assert not corrections, "보정 없어야 함"

    def test_player_as_target(self):
        action, corrections = validate_and_clamp_action(
            make_action(action_type="Attack", target_id="Player", params={"damage": "20"})
        )
        assert action is not None, "'Player'는 유효한 target_id"

    def test_no_target_id(self):
        # Wait 는 ACTION_REQUIRED_PARAMS 미등재 → target_id 없이 통과.
        action, _ = validate_and_clamp_action(make_action(action_type="Wait", target_id=None))
        assert action is not None, "target_id 없는 액션은 통과해야 함"

    def test_invalid_npc_id(self):
        action, corrections = validate_and_clamp_action(
            make_action(action_type="Attack", target_id="Goblin_999_Hallucinated")
        )
        # VALID_NPC_IDS 는 actions.py 정적 상수로 항상 채워짐 → 환각 ID 는 반드시 차단.
        assert action is None, "환각 NPC ID는 차단되어야 함"


class TestCoordinateValidation:
    """좌표 범위 검증 테스트."""

    def test_valid_coordinates(self):
        # Move 는 필수 파라미터 없음 → target_loc 만으로 통과.
        action, _ = validate_and_clamp_action(
            make_action(action_type="Move", target_loc=_loc(100.0, 200.0, 50.0), params={"style": "Walk"})
        )
        assert action is not None, "정상 좌표는 통과해야 함"

    def test_out_of_bounds_coordinates(self):
        action, corrections = validate_and_clamp_action(
            make_action(action_type="Move", target_loc=_loc(999999.0, 999999.0, 0.0), params={"style": "Walk"})
        )
        # WORLD_BOUNDS 는 actions.py 정적 상수로 항상 채워짐 → 경계 밖 좌표는 반드시 차단.
        assert action is None, "경계 밖 좌표는 차단되어야 함"
        assert any("WORLD_BOUNDS" in c or "이탈" in c or "밖" in c for c in corrections)


class TestValueClamping:
    """수치 파라미터 클램핑 테스트."""

    def test_damage_over_max(self):
        # Attack 은 target_id 필수 → Player 지정 후 damage 클램핑 검증.
        action, corrections = validate_and_clamp_action(
            make_action(action_type="Attack", target_id="Player", params={"damage": "9999"})
        )
        assert action is not None
        assert any("클램핑" in c or "clamp" in c.lower() for c in corrections), (
            "damage 초과 → 클램핑 보정이 기록되어야 함"
        )
        assert float(action.Parameters["damage"]) <= 100

    def test_speed_over_max(self):
        action, corrections = validate_and_clamp_action(
            make_action(action_type="Move", params={"speed": "9999", "style": "Run"})
        )
        assert action is not None
        assert float(action.Parameters["speed"]) <= 600


def run_all_tests():
    """모든 테스트를 실행하고 결과를 출력한다."""
    test_classes = [TestTargetIdValidation, TestCoordinateValidation, TestValueClamping]

    passed = 0
    failed = 0

    print("=== Rules Validation 테스트 시작 ===\n")

    for cls in test_classes:
        print(f"--- {cls.__name__} ---")
        instance = cls()
        test_methods = [m for m in dir(instance) if m.startswith("test_")]

        for method_name in test_methods:
            try:
                getattr(instance, method_name)()
                print(f"  ✅ PASS: {method_name}")
                passed += 1
            except AssertionError as e:
                print(f"  ❌ FAIL: {method_name} → {e}")
                failed += 1
            except Exception as e:
                print(f"  ⚠️  ERROR: {method_name} → {e}")
                failed += 1
        print()

    print(f"=== 결과: {passed}개 통과 / {failed}개 실패 ===")
    return failed == 0


if __name__ == "__main__":
    success = run_all_tests()
    sys.exit(0 if success else 1)
