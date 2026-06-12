"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: rules.py                                                               ║
║ Role: VALIDATOR (게임 규칙 심판)                                              ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ 핵심 역할 (변경 금지):                                                        ║
║   ActionBatch를 게임 규칙에 따라 검증하고 보정한다. LLM 없이 순수 Python.      ║
║                                                                              ║
║ [신규] world_constants.json 활용 검증:                                       ║
║   - target_id가 valid_npc_ids 목록에 없으면 해당 액션 제거                  ║
║   - target_loc 좌표가 WORLD_BOUNDS 밖이면 해당 액션 제거                    ║
║                                                                              ║
║ 기존 검증:                                                                   ║
║   - Attack.damage: [0, MAX_DAMAGE] 클램핑                                   ║
║   - Move.speed: [0, MAX_SPEED] 클램핑                                       ║
║                                                                              ║
║ 설계 원칙:                                                                   ║
║   LLM 없음 → 토큰 비용 Zero, 빠른 결정론적 검증.                           ║
║   범위 초과 값 → 클램핑 (삭제 아님). 없는 타겟 → 액션 제거.               ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""

from collections import Counter

from ..state import AgentState
from ...schemas.actions import (
    ACTION_CATEGORY,
    ACTION_REQUIRED_PARAMS,
    ActionBatch,
    GameAction,
    WORLD_CONSTANTS,
)
from ...utils import db_manager


# world_constants.json에서 유효 ID 목록 및 월드 경계 로드
VALID_NPC_IDS: set[str] = set(WORLD_CONSTANTS.get("valid_npc_ids", []))
VALID_LOCATION_IDS: set[str] = set(WORLD_CONSTANTS.get("valid_location_ids", []))
WORLD_BOUNDS: dict = WORLD_CONSTANTS.get("WORLD_BOUNDS", {})


def _is_target_id_valid(target_id: str | None) -> bool:
    """
    target_id가 월드에 존재하는 유효한 NPC/Actor인지 검증한다.

    왜 필요한가: LLM이 "Goblin_999" 같은 존재하지 않는 ID를 생성하면
    UE5 C++에서 타겟을 찾지 못해 조용히 실패(silent fail)하게 됨.
    서버 단에서 선제 차단하는 것이 더 안전하고 디버깅이 쉬움.

    Returns: True이면 유효 (또는 None이라 검증 불필요)
    """
    if not target_id:
        return True  # target_id 없는 액션은 타겟 없이 실행 가능

    # valid_npc_ids가 빈 목록이면 검증 자체를 건너뜀 (설정 미완료 대비)
    if not VALID_NPC_IDS:
        return True

    return target_id in VALID_NPC_IDS


def _is_target_loc_in_bounds(target_loc_str: str | None) -> bool:
    """
    target_loc 좌표가 월드 경계(WORLD_BOUNDS) 안에 있는지 검증한다.

    왜 필요한가: LLM이 좌표를 환각(hallucination)으로 생성하면
    NPC가 맵 밖으로 텔레포트하는 버그가 발생할 수 있음.

    Returns: True이면 경계 내 (또는 WORLD_BOUNDS 미설정)
    """
    if not target_loc_str or not WORLD_BOUNDS:
        return True

    try:
        import ast
        import re

        coords: dict = {}
        # ① dict/JSON 문자열 시도
        try:
            parsed = ast.literal_eval(target_loc_str)
            if isinstance(parsed, dict):
                coords = {str(k).lower(): float(v) for k, v in parsed.items()}
        except Exception:
            pass
        # ② UE 형식 (X=100,Y=200,Z=0) 폴백 — literal_eval 로는 SyntaxError
        if not coords:
            coords = {
                k.lower(): float(v)
                for k, v in re.findall(
                    r"([XYZxyz])\s*=\s*(-?\d+(?:\.\d+)?)", target_loc_str
                )
            }
        if not coords:
            # 파싱 실패 → (0,0,0) 오판 대신 경계검증 생략(통과)
            return True
        x, y, z = coords.get("x", 0), coords.get("y", 0), coords.get("z", 0)
    except Exception:
        # 파싱 자체 실패 시 경계검증 생략(통과)
        return True

    x_ok = (
        WORLD_BOUNDS.get("x_min", float("-inf"))
        <= x
        <= WORLD_BOUNDS.get("x_max", float("inf"))
    )
    y_ok = (
        WORLD_BOUNDS.get("y_min", float("-inf"))
        <= y
        <= WORLD_BOUNDS.get("y_max", float("inf"))
    )
    z_ok = (
        WORLD_BOUNDS.get("z_min", float("-inf"))
        <= z
        <= WORLD_BOUNDS.get("z_max", float("inf"))
    )

    return x_ok and y_ok and z_ok


def _missing_required_group(action: "GameAction") -> str | None:
    """
    ACTION_REQUIRED_PARAMS 기준 필수 파라미터 충족 검사.

    왜 필요한가: 필수 키 누락 액션은 C++ 에서 무음 no-op(Follow/Attack/Track 등)
    또는 원점(0,0,0) 이동 버그(PickUp/Investigate)로 이어짐. 서버 단 선제 제거.

    Returns: 미충족 그룹의 키 목록 문자열, 모두 충족이면 None
    """
    groups = ACTION_REQUIRED_PARAMS.get(action.ActionType)
    if not groups:
        return None
    params = action.Parameters or {}
    for group in groups:
        if not any(str(params.get(k) or "").strip() for k in group):
            return " | ".join(group)
    return None


def validate_and_clamp_action(action: "GameAction") -> tuple:
    """
    단일 액션의 파라미터를 검증하고 범위를 보정(clamp)한다.

    Returns:
        (action | None, corrections: list[str])
        - action=None이면 이 액션은 완전히 제거해야 함
        - corrections는 로깅용 보정 내용 목록
    """
    corrections = []

    params = action.Parameters or {}
    target_id = params.get("target_id")
    target_loc_str = params.get("target_loc")

    # ── 필수 파라미터 검증 ──────────────────────────────────────
    missing = _missing_required_group(action)
    if missing:
        reason = f"{action.ActionType} 필수 파라미터 누락 ({missing}) → 액션 제거"
        print(f"[Rules] ❌ {reason}")
        return None, [reason]

    # ── [신규] 타겟 ID 검증 ─────────────────────────────────────
    if not _is_target_id_valid(target_id):
        reason = f"유효하지 않은 target_id '{target_id}' → 액션 제거"
        print(f"[Rules] ❌ {reason}")
        return None, [reason]

    # ── [신규] 좌표 범위 검증 ───────────────────────────────────
    if not _is_target_loc_in_bounds(target_loc_str):
        reason = (
            f"target_loc {target_loc_str} 이 WORLD_BOUNDS 밖 → 액션 제거 "
            f"(action: {action.ActionType})"
        )
        print(f"[Rules] ❌ {reason}")
        return None, [reason]

    # Dialogue 액션은 수치 파라미터 없음 → 검증 불필요
    if action.ActionType == "Dialogue":
        return action, corrections

    if not action.Parameters:
        return action, corrections

    # 파라미터를 문자열로 통일 (C++ 호환성)
    params = {str(k): str(v) for k, v in action.Parameters.items()}

    # ── Attack: damage 클램핑 ───────────────────────────────────
    if action.ActionType == "Attack" and "damage" in params:
        try:
            damage = float(params["damage"])
            max_damage = WORLD_CONSTANTS.get("MAX_DAMAGE", 100)
            if damage > max_damage:
                params["damage"] = str(max_damage)
                corrections.append(f"damage 클램핑: {damage} → {max_damage}")
            elif damage < 0:
                params["damage"] = "0"
                corrections.append("damage 음수 → 0")
        except ValueError:
            params["damage"] = "10"
            corrections.append("damage 비유효 → 기본값 10")

    # ── Move: speed 클램핑 ──────────────────────────────────────
    elif action.ActionType == "Move" and "speed" in params:
        try:
            speed = float(params["speed"])
            max_speed = WORLD_CONSTANTS.get("MAX_SPEED", 600)
            if speed > max_speed:
                params["speed"] = str(max_speed)
                corrections.append(f"speed 클램핑: {speed} → {max_speed}")
            elif speed < 0:
                params["speed"] = "300"
                corrections.append("speed 음수 → 기본값 300")
        except ValueError:
            params["speed"] = "300"
            corrections.append("speed 비유효 → 기본값 300")

    # ── Heal: amount 클램핑 ─────────────────────────────────────
    elif action.ActionType == "Heal" and "amount" in params:
        try:
            amount = float(params["amount"])
            max_health = WORLD_CONSTANTS.get("MAX_HEALTH", 100)
            if amount > max_health:
                params["amount"] = str(max_health)
                corrections.append(f"heal amount 클램핑: {amount} → {max_health}")
            elif amount < 0:
                params["amount"] = "0"
                corrections.append("heal amount 음수 → 0")
        except ValueError:
            params["amount"] = "10"
            corrections.append("heal amount 비유효 → 기본값 10")

    action.Parameters = params
    return action, corrections


def _validate_batch(batch: "ActionBatch") -> "ActionBatch":
    """단일 ActionBatch 검증/클램핑. 공통 로직."""
    from ...schemas.actions import GameAction

    all_corrections: list[str] = []
    validated_actions: list[GameAction] = []

    for action in batch.Actions:
        validated_action, corrections = validate_and_clamp_action(action)
        if validated_action is None:
            all_corrections.extend(corrections)
            continue
        validated_actions.append(validated_action)
        all_corrections.extend(corrections)

    if not validated_actions:
        print(
            f"[Rules] ❌ {batch.AgentID} 모든 액션 검증 실패. 이유: {'; '.join(all_corrections)}"
        )
        batch.Actions = []
        return batch

    batch.Actions = validated_actions
    _correct_mode_mismatch(batch)
    if all_corrections:
        summary = "; ".join(all_corrections)
        print(f"[Rules] ✅ {batch.AgentID} {len(all_corrections)}개 보정: {summary}")
    else:
        print(f"[Rules] ✅ {batch.AgentID} 검증 통과")
    return batch


def _correct_mode_mismatch(batch: "ActionBatch") -> None:
    """
    Mode↔액션 카테고리 불일치 보정 (액션 제거 아님 — 덜 파괴적).

    규칙: 비-Common 액션들의 다수 카테고리가 Mode 와 다르고, Mode 가 어떤
    액션 카테고리와도 일치하지 않으면 Mode 를 다수 카테고리로 교정.
    Common 전용 배치는 Mode 유지 — Combat 모드 중 대사(Dialogue)는 정상이므로.
    """
    non_common = [
        cat
        for a in batch.Actions
        if (cat := ACTION_CATEGORY.get(a.ActionType, "Common")) != "Common"
    ]
    if not non_common:
        return
    majority, _count = Counter(non_common).most_common(1)[0]
    if batch.Mode != majority and batch.Mode not in non_common:
        print(
            f"[Rules] 🔧 Mode 보정: {batch.Mode} → {majority} "
            f"({batch.AgentID}, 액션 카테고리 불일치)"
        )
        batch.Mode = majority


def rules_node(state: AgentState) -> dict:
    """
    Rules Agent (LLM 없는 순수 Python 검증).

    멀티 NPC: action_batches Dict 전체 루프.
    단일 NPC 호환: action_batches 없으면 action_batch 단일 처리.
    """
    action_batches: dict = state.get("action_batches") or {}

    # 단일 NPC 호환 경로
    if not action_batches:
        batch = state.get("action_batch")
        if not batch:
            print("[Rules] ActionBatch 없음, 조용히 종료")
            return {"next": "End", "current_speaker": "Rules"}
        action_batches = {batch.AgentID: batch}

    validated_batches: dict = {}
    for npc_id, batch in action_batches.items():
        validated_batches[npc_id] = _validate_batch(batch)
        _evaluate_and_update_affinity(state, validated_batches[npc_id])

    # 단일 NPC 호환: action_batch 도 채움
    first_batch = next(iter(validated_batches.values()), None)

    return {
        "action_batches": validated_batches,
        "action_batch": first_batch,
        "current_speaker": "Rules",
        "next": "End",
    }


def _evaluate_and_update_affinity(state: AgentState, batch: "ActionBatch"):
    """
    ActionBatch에 담긴 행동과 감정을 분석하여
    대상(Player 등)에 대한 우호도(Affinity)를 조정한다.
    규칙(Rule) 기반으로 점수를 증감시킨 뒤 DB Manager 캐시에 즉시 반영.
    """
    # 1. Player ID와 Source(NPC) ID 확인
    # vr_context가 엉망이거나 null이면 건너뜀 (MVP용 방어코드)
    vr_context = state.get("vr_context")
    if not vr_context:
        return

    player_id = (
        vr_context.get("player_id", "Player")
        if isinstance(vr_context, dict)
        else getattr(vr_context, "player_id", "Player")
        if vr_context
        else "Player"
    )
    npc_id = batch.AgentID

    if not npc_id:
        return

    # 2. 이번 턴에 반영될 점수 (score_delta)
    score_delta = 0
    interaction_summary = []

    # 전체 감정에 따른 기본 보정치 (FacialState가 batch 레벨에는 없으므로 첫 번째 액션 참조 또는 생략)
    # ActionBatch는 Mode만 가짐, 개별 Action에 FacialState가 있음. 여기선 단순히 0으로 시작.

    # 구체적 액션 평가
    for action in batch.Actions:
        params = action.Parameters or {}
        target_id = params.get("target_id", "")

        # Player를 대상으로 한 액션인지 확인
        if target_id and target_id.lower() == player_id.lower():
            if action.ActionType == "Attack":
                score_delta -= 10
                interaction_summary.append("Attacked player (-10)")
            elif action.ActionType == "Heal":
                score_delta += 5
                interaction_summary.append("Healed player (+5)")
            elif action.ActionType == "Dialogue":
                facial = action.FacialState
                if facial == "Happy":
                    score_delta += 2
                    interaction_summary.append("Spoke happily (+2)")
                elif facial == "Angry":
                    score_delta -= 2
                    interaction_summary.append("Spoke angrily (-2)")

    # 3. 점수 변화가 있다면 DB 매니저를 통해 캐시 업데이트
    if score_delta != 0:
        summary_str = ", ".join(interaction_summary)
        print(
            f"[Rules] 🎯 Affinity Delta for {npc_id} -> {player_id}: {score_delta} ({summary_str})"
        )
        # 비동기 환경 내에서 안전하게 동기 함수 호출 (캐싱만 하므로 빠름)
        db_manager.update_affinity_sync(
            source_id=npc_id,
            target_id=player_id,
            score_delta=score_delta,
            interaction_summary=summary_str,
        )
