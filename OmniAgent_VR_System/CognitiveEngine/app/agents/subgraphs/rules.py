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

import logging
import ast
import re
from collections import Counter

from ..state import AgentState
from ...utils.train_logger import log_rules_result
from ...schemas.actions import (
    ACTION_CATEGORY,
    ACTION_REQUIRED_PARAMS,
    ActionBatch,
    GameAction,
    WORLD_CONSTANTS,
)
from ...utils import db_manager


logger = logging.getLogger(__name__)

# world_constants.json에서 유효 ID 목록 및 월드 경계 로드
VALID_NPC_IDS: set[str] = set(WORLD_CONSTANTS.get("valid_npc_ids", []))
WORLD_BOUNDS: dict = WORLD_CONSTANTS.get("WORLD_BOUNDS", {})

# C++ ResolveActionTarget 이 항상 해석하는 센티넬 — 런타임 목록에 무조건 합집합.
_SENTINEL_TARGETS: set[str] = {"Player", "Self", "Enemy"}


def _is_target_id_valid(target_id: str | None, runtime_targets: set[str] | None = None) -> bool:
    """
    target_id가 월드에 존재하는 유효한 NPC/Actor인지 검증한다.

    왜 필요한가: LLM이 "Goblin_999" 같은 존재하지 않는 ID를 생성하면
    UE5 C++에서 타겟을 찾지 못해 조용히 실패(silent fail)하게 됨.
    서버 단에서 선제 차단하는 것이 더 안전하고 디버깅이 쉬움.

    runtime_targets: UE5 prompt 동봉 valid_targets(런타임 등록 NPC) — 있으면 이것이
    진실(정적 목록보다 우선). 없으면 WORLD_CONSTANTS 정적 폴백. 정적 목록만 쓰던
    구현은 실존 NPC(Skadi/Moca) 타겟 액션을 조용히 제거하던 버그(2026-07-09 수정).

    Returns: True이면 유효 (또는 None이라 검증 불필요)
    """
    if not target_id:
        return True  # target_id 없는 액션은 타겟 없이 실행 가능

    # 빈 set(UE5 가 valid_targets:[] 명시 = 등록 NPC 없음)은 센티넬만 허용 — static 폴백 금지.
    # runtime 이 진실이므로 None(정보 없음)일 때만 static 으로 내려간다.
    if runtime_targets is not None:
        return target_id in (runtime_targets | _SENTINEL_TARGETS)

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
            coords = {k.lower(): float(v) for k, v in re.findall(r"([XYZxyz])\s*=\s*(-?\d+(?:\.\d+)?)", target_loc_str)}
        if not coords:
            # 파싱 실패 → (0,0,0) 오판 대신 경계검증 생략(통과)
            return True
    except Exception:
        # 파싱 자체 실패 시 경계검증 생략(통과)
        return True

    for axis in ("x", "y", "z"):
        value = coords.get(axis, 0)
        lo = WORLD_BOUNDS.get(f"{axis}_min", float("-inf"))
        hi = WORLD_BOUNDS.get(f"{axis}_max", float("inf"))
        if not (lo <= value <= hi):
            return False

    return True


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
        if not any(params.get(k) is not None and str(params.get(k)).strip() != "" for k in group):
            return " | ".join(group)
    return None


# ActionType → (param_key, 상한 상수키, 상한 기본, 음수시 값, 음수 로그표기, 비유효시 값, 로그 라벨)
# Attack.damage / Move.speed 의 [0, 상한] 클램핑을 단일 테이블로 통합.
_NUMERIC_CLAMP_RULES: dict[str, tuple] = {
    "Attack": ("damage", "MAX_DAMAGE", 100, "0", "0", "10", "damage"),
    "Move": ("speed", "MAX_SPEED", 600, "300", "기본값 300", "300", "speed"),
}


def _clamp_numeric_param(action_type: str, params: dict, corrections: list) -> None:
    """수치 파라미터를 [0, WORLD_CONSTANTS 상한] 으로 클램핑. 규칙 없는 액션은 no-op."""
    rule = _NUMERIC_CLAMP_RULES.get(action_type)
    if not rule:
        return
    key, max_key, max_default, neg_val, neg_disp, invalid_val, label = rule
    if key not in params:
        return
    max_value = WORLD_CONSTANTS.get(max_key, max_default)
    try:
        value = float(params[key])
        if value > max_value:
            params[key] = str(max_value)
            corrections.append(f"{label} 클램핑: {value} → {max_value}")
        elif value < 0:
            params[key] = neg_val
            corrections.append(f"{label} 음수 → {neg_disp}")
    except (ValueError, TypeError):
        params[key] = invalid_val
        corrections.append(f"{label} 비유효 → 기본값 {invalid_val}")


def validate_and_clamp_action(action: "GameAction", runtime_targets: set[str] | None = None) -> tuple:
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
        logger.warning(f"[Rules] X {reason}")
        return None, [reason]

    # ── [신규] 타겟 ID 검증 ─────────────────────────────────────
    if not _is_target_id_valid(target_id, runtime_targets):
        reason = f"유효하지 않은 target_id '{target_id}' → 액션 제거"
        logger.warning(f"[Rules] X {reason}")
        return None, [reason]

    # ── [신규] 좌표 범위 검증 ───────────────────────────────────
    if not _is_target_loc_in_bounds(target_loc_str):
        reason = f"target_loc {target_loc_str} 이 WORLD_BOUNDS 밖 → 액션 제거 (action: {action.ActionType})"
        logger.warning(f"[Rules] X {reason}")
        return None, [reason]

    # Dialogue 액션은 수치 파라미터 없음 → 검증 불필요
    if action.ActionType == "Dialogue":
        return action, corrections

    if not action.Parameters:
        return action, corrections

    # 파라미터를 문자열로 통일 (C++ 호환성)
    params = {str(k): str(v) for k, v in action.Parameters.items()}

    # ── 수치 파라미터 클램핑 (Attack.damage / Move.speed / Heal.amount) ──
    _clamp_numeric_param(action.ActionType, params, corrections)

    action.Parameters = params
    return action, corrections


def _validate_batch(
    batch: "ActionBatch", runtime_targets: set[str] | None = None, log_ctx: dict | None = None
) -> "ActionBatch":
    """단일 ActionBatch 검증/클램핑. 공통 로직.
    log_ctx: 파인튜닝 로그 조인 키({"msg_id","attempt"}) — 지정 시 판정 결과 jsonl 기록."""
    from ...schemas.actions import GameAction

    all_corrections: list[str] = []
    validated_actions: list[GameAction] = []
    actions_before = len(batch.Actions)
    mode_before = batch.Mode

    for action in batch.Actions:
        validated_action, corrections = validate_and_clamp_action(action, runtime_targets)
        if validated_action is None:
            all_corrections.extend(corrections)
            continue
        validated_actions.append(validated_action)
        all_corrections.extend(corrections)

    if not validated_actions:
        logger.error(f"[Rules] X {batch.AgentID} 모든 액션 검증 실패. 이유: {'; '.join(all_corrections)}")
        batch.Actions = []
    else:
        batch.Actions = validated_actions
        _correct_mode_mismatch(batch)
        if all_corrections:
            summary = "; ".join(all_corrections)
            logger.info(f"[Rules] OK {batch.AgentID} {len(all_corrections)}개 보정: {summary}")
        else:
            logger.info(f"[Rules] OK {batch.AgentID} 검증 통과")

    if log_ctx is not None:
        log_rules_result(
            npc_id=batch.AgentID,
            actions_before=actions_before,
            actions_after=len(batch.Actions),
            corrections=all_corrections,
            mode_before=mode_before,
            mode_after=batch.Mode,
            extra=log_ctx,
        )
    return batch


def _correct_mode_mismatch(batch: "ActionBatch") -> None:
    """
    Mode↔액션 카테고리 불일치 보정 (액션 제거 아님 — 덜 파괴적).

    규칙: 비-Common 액션들의 다수 카테고리가 Mode 와 다르고, Mode 가 어떤
    액션 카테고리와도 일치하지 않으면 Mode 를 다수 카테고리로 교정.
    Common 전용 배치는 Mode 유지 — Combat 모드 중 대사(Dialogue)는 정상이므로.
    """
    non_common = [cat for a in batch.Actions if (cat := ACTION_CATEGORY.get(a.ActionType, "Common")) != "Common"]
    if not non_common:
        return
    majority, _count = Counter(non_common).most_common(1)[0]
    if batch.Mode != majority and batch.Mode not in non_common:
        logger.info(f"[Rules] FIX Mode 보정: {batch.Mode} → {majority} ({batch.AgentID}, 액션 카테고리 불일치)")
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
            logger.warning("[Rules] ActionBatch 없음, 조용히 종료")
            return {"next": "End", "current_speaker": "Rules"}
        action_batches = {batch.AgentID: batch}

    # UE5 prompt 동봉 valid_targets(런타임 등록 NPC) — 타겟 검증의 우선 진실.
    # GesPrompt 가 List[str] 로 검증하므로 문자열이 통째로 오는 경우는 여기 도달 전에 걸러진다.
    runtime_list = state["vr_context"].valid_targets
    runtime_targets: set[str] | None = set(runtime_list) if runtime_list else None

    # 파인튜닝 로그 조인 키 — Stage1 LLM 레코드와 msg_id+attempt 로 매칭 (train_logger)
    log_ctx = {"msg_id": state.get("msg_id", ""), "attempt": state.get("rules_retry_count", 0)}

    validated_batches: dict = {}
    for npc_id, batch in action_batches.items():
        validated_batches[npc_id] = _validate_batch(batch, runtime_targets, log_ctx)
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
    player_id = state["vr_context"].player_id or "Player"
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
        logger.info(f"[Rules] AFFINITY Affinity Delta for {npc_id} -> {player_id}: {score_delta} ({summary_str})")
        # 비동기 환경 내에서 안전하게 동기 함수 호출 (캐싱만 하므로 빠름)
        db_manager.update_affinity_sync(
            source_id=npc_id,
            target_id=player_id,
            score_delta=score_delta,
            interaction_summary=summary_str,
        )
