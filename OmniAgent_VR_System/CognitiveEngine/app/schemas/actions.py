"""
File: actions.py
Purpose: Defines the 'ActionBatch' data contract for Engine Commands.

[C++ 연동 주의]
- behavior_mode: C++ ENPCBehaviorMode Enum 값과 정확히 일치해야 BehaviorTree가 올바른 서브트리를 선택함.
- facial_state:  C++ EFacialState Enum 값과 정확히 일치해야 AnimBlueprint가 표정을 제어함.
- action_type:   각 모드별 C++ Enum(ECommonAction, ECombatAction 등)과 1:1 대응해야
                 BTTask의 switch문이 올바르게 분기됨.
- 허용 값 외의 문자열이 들어오면 Pydantic이 서버 단에서 즉시 ValidationError를 발생시킴.
"""
from pydantic import BaseModel, Field, model_validator, field_serializer
from typing import List, Optional, Literal, Union
import json
import os

# Load Shared Data Constants
SHARED_DATA_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../SharedData/world_constants.json"))
try:
    with open(SHARED_DATA_PATH, "r") as f:
        WORLD_CONSTANTS = json.load(f)
except FileNotFoundError:
    print(f"WARNING: world_constants.json not found at {SHARED_DATA_PATH}. Using defaults.")
    WORLD_CONSTANTS = {"MAX_DAMAGE": 100}


# ─────────────────────────────────────────────────────────────────────────────
# C++ ENPCBehaviorMode Enum과 1:1 대응 (BTTask_BaseDefinitions.h 참조)
# BehaviorTree가 이 값으로 서브트리를 분기하므로 오타 시 항상 Common으로 고정됨.
# ─────────────────────────────────────────────────────────────────────────────
NPCBehaviorMode = Literal[
    "None",          # 초기 상태 / 오류
    "Combat",        # 전투 모드
    "Social",        # 사교 모드
    "Task",          # 상호작용/작업 모드
    "Investigation", # 탐색/조사 모드
    "Lifestyle",     # 생활/대기 모드
    "Common",        # 공용/기본 액션 모드 (기본값)
]

# ─────────────────────────────────────────────────────────────────────────────
# C++ EFacialState Enum과 1:1 대응 (BTTask_BaseDefinitions.h 참조)
# AnimBlueprint가 이 값으로 Facial Blend Shape를 제어함.
# ─────────────────────────────────────────────────────────────────────────────
NPCFacialState = Literal[
    "Neutral",    # 평온 (기본)
    "Happy",      # 기쁨
    "Sad",        # 슬픔
    "Angry",      # 분노
    "Fear",       # 공포
    "Surprised",  # 놀람
    "Disgusted",  # 혐오
    "Tired",      # 피곤
    "Pain",       # 고통
]


# ─────────────────────────────────────────────────────────────────────────────
# 각 모드(Category)별 허용되는 action_type 목록
# C++ BTTask_*Actions.h의 Enum 값과 정확히 1:1 대응
# ─────────────────────────────────────────────────────────────────────────────

# BTTask_CommonActions.h → ECommonAction
CommonActionType = Literal[
    "Idle",       # 기본 대기 상태
    "Move",       # 특정 지점으로 이동
    "Follow",     # 대상 따라가기
    "Wait",       # 제자리 대기 (Duration)
    "Dialogue",   # 대사 (전투 외침, 혼잣말, 대화)
    "TurnTo",     # 특정 대상 바라보기 (회전)
    "Stop",       # 현재 행동 즉시 중단
    "Scan",       # 주변 두리번거리기
    "UseItem",    # 아이템 사용 (포션, 음식)
    "Equip",      # 장비 착용
    "Unequip",    # 장비 해제
]

# BTTask_CombatActions.h → ECombatAction
CombatActionType = Literal[
    "Attack",       # 대상 공격
    "Block",        # 방어/패링
    "Dodge",        # 회피 (구르기)
    "Flee",         # 도주 (전투 이탈)
    "SignalAllies", # 아군에게 신호 (지원 요청, 포위 등)
]

# BTTask_SocialActions.h → ESocialAction
SocialActionType = Literal[
    "Trade",        # 거래/협상
    "Follow",       # 대상 따라가기 (Social 맥락)
    "Emote",        # 감정 제스처 (인사, 끄덕임)
    "GiveItem",     # 아이템 전달 (선물)
    "Comfort",      # 위로
    "HandObject",   # VR 특화: 플레이어 손에 물건 건네기
]

# BTTask_TaskActions.h → ETaskAction
TaskActionType = Literal[
    "PickUp",   # 아이템 줍기
    "Drop",     # 아이템 버리기
    "Craft",    # 아이템 제작
    "Repair",   # 오브젝트/장비 수리
]

# BTTask_InvestigationActions.h → EInvestigationAction
InvestigationActionType = Literal[
    "Investigate",  # 의심 지점 수색
    "Track",        # 흔적 추적
    "Scout",        # 주변 반경 정찰
]

# BTTask_LifestyleActions.h → ELifestyleAction
LifestyleActionType = Literal[
    "Sit",    # 앉기
    "Sleep",  # 수면
    "Clean",  # 청소
    "Read",   # 독서
    "Pray",   # 기도
    "Dance",  # 춤
    "Sing",   # 노래
]

# 모든 action_type을 합친 Union Literal
# LLM이 생성할 수 있는 전체 action_type 목록
AllActionType = Literal[
    # Common
    "Idle", "Move", "Follow", "Wait", "Dialogue", "TurnTo", "Stop", "Scan", "UseItem", "Equip", "Unequip",
    # Combat
    "Attack", "Block", "Dodge", "Flee", "SignalAllies",
    # Social
    "Trade", "Emote", "GiveItem", "Comfort", "HandObject",
    # Task
    "PickUp", "Drop", "Craft", "Repair",
    # Investigation
    "Investigate", "Track", "Scout",
    # Lifestyle
    "Sit", "Sleep", "Clean", "Read", "Pray", "Dance", "Sing",
]

# 런타임 검증용: category → 허용 action_type 매핑 딕셔너리
# 왜 필요한가: LLM이 category="Combat"인데 action_type="Sit"을 보내면
# C++ BT 서브트리에서 절대 매칭되지 않으므로 서버단에서 선제 차단.
CATEGORY_ACTION_MAP: dict[str, set[str]] = {
    "Common":        {"Idle", "Move", "Follow", "Wait", "Dialogue", "TurnTo", "Stop", "Scan", "UseItem", "Equip", "Unequip"},
    "Combat":        {"Attack", "Block", "Dodge", "Flee", "SignalAllies"},
    "Social":        {"Trade", "Follow", "Emote", "GiveItem", "Comfort", "HandObject"},
    "Task":          {"PickUp", "Drop", "Craft", "Repair"},
    "Investigation": {"Investigate", "Track", "Scout"},
    "Lifestyle":     {"Sit", "Sleep", "Clean", "Read", "Pray", "Dance", "Sing"},
}


# ─────────────────────────────────────────────────────────────────────────────
# 3D 좌표 전용 자료형 (C++ FVector와 1:1 대응)
# 왜 dict가 아닌 전용 모델인가:
#   1. 타입 안전성: LLM이 x/y/z를 빠뜨리면 Pydantic이 즉시 에러 발생
#   2. 직렬화 일관성: JSON 출력이 항상 {"x": float, "y": float, "z": float}
#   3. C++ 연동: MCPJsonUtils에서 FVector로 바로 변환 가능
# ─────────────────────────────────────────────────────────────────────────────
class GameVector3(BaseModel):
    """C++ FVector와 1:1 대응되는 3D 좌표 모델."""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def to_dict(self) -> dict:
        """parameters dict 안에 중첩될 때 사용."""
        return {"x": self.x, "y": self.y, "z": self.z}

    def __str__(self) -> str:
        return f"({self.x}, {self.y}, {self.z})"


class NPCAction(BaseModel):
    """
    NPC가 실행할 단일 액션.

    [action_category ↔ action_type 관계]
    - action_category는 C++ BehaviorTree의 어떤 서브트리에서 처리될지 결정.
    - action_type은 해당 서브트리 내에서 switch 분기의 기준.
    - 두 값이 불일치하면 C++에서 action을 찾지 못하므로, model_validator로 방어.

    [C++ 연동 흐름]
    SmartNPC::DispatchActions() → Blackboard(SubAction=action_type) →
    BTTask_{Category}Action::ExecuteTask() → switch(action_type) → Execute*()

    [필드별 사용 규칙]
    - "Dialogue" : parameters에 text(필수), tone(선택) 사용
    - "Move"     : target_loc 필드에 GameVector3 사용 (parameters.target_loc → 자동 승격)
    - "Attack"   : target_id(필수), parameters에 weapon_slot(선택) 사용
    - 그 외      : 각 C++ Enum 주석의 Parameters 참조
    """
    # 액션 분류 (어떤 BT 서브트리에서 처리할지 결정)
    action_category: Literal["Common", "Combat", "Social", "Task", "Investigation", "Lifestyle"]

    # 액션 종류 (서브트리 내 switch 분기 기준, C++ Enum과 1:1 대응)
    action_type: AllActionType

    # 이 액션을 실제로 수행할 NPC의 ID (멀티-NPC 시나리오에서 라우팅 키)
    executor_npc_id: str

    # 감정 상태 (표정 연동)
    emotion: str = "Neutral"

    # 대상 Actor ID (공격 대상, 따라갈 대상 등)
    target_id: Optional[str] = None

    # 이동 목표 좌표 (Move, Follow 등에서 사용. C++ FVector로 직접 변환됨)
    # parameters.target_loc에 dict로 들어오면 자동 승격됨
    target_loc: Optional[GameVector3] = None

    # 액션별 추가 파라미터
    # 예: {"text": "Hello"}, {"weapon_slot": "0"}
    # 주의: target_loc은 여기에 넣지 말고 위 필드를 사용할 것
    parameters: dict = Field(default_factory=dict)

    @model_validator(mode="after")
    def validate_and_normalize(self):
        """
        1. parameters.target_loc → self.target_loc으로 자동 승격
        2. category ↔ action_type 불일치 차단
        """
        # --- target_loc 자동 승격 ---
        # LLM이 parameters 안에 target_loc을 넣을 수 있으므로, 꺼내서 전용 필드로 이동
        if "target_loc" in self.parameters and self.target_loc is None:
            loc_data = self.parameters.pop("target_loc")
            if isinstance(loc_data, dict):
                self.target_loc = GameVector3(**loc_data)
            elif isinstance(loc_data, GameVector3):
                self.target_loc = loc_data

        # --- category ↔ action_type 검증 ---
        allowed = CATEGORY_ACTION_MAP.get(self.action_category, set())

        # CommonAction은 C++ ExecuteCommonFallback 패턴으로 모든 모드에서 실행 가능
        UNIVERSAL_ACTIONS = {
            "Idle", "Move", "Follow", "Wait", "Dialogue",
            "TurnTo", "Stop", "Scan", "UseItem", "Equip", "Unequip",
        }

        if self.action_type not in allowed and self.action_type not in UNIVERSAL_ACTIONS:
            raise ValueError(
                f"action_type '{self.action_type}' is not allowed in category '{self.action_category}'. "
                f"Allowed types: {sorted(allowed)}"
            )
        return self

    @field_serializer('target_loc')
    @classmethod
    def serialize_target_loc(cls, v: Optional[GameVector3], _info):
        """JSON 직렬화 시 GameVector3 → dict로 변환. C++ 파싱과 호환."""
        if v is None:
            return None
        return v.to_dict()


class RejectResult(BaseModel):
    reason: str
    rejected: bool = True


class ActionBatch(BaseModel):
    """
    C++ FActionBatch 구조체와 1:1 대응 (MCPJsonUtils.h 참조).

    필드 매핑:
      agent_id       → FActionBatch.AgentID       (NPCManager가 NPC를 찾는 라우팅 키)
      behavior_mode  → FActionBatch.BehaviorMode   (BehaviorTree 서브트리 분기 기준)
      facial_state   → FActionBatch.FacialState    (AnimBlueprint 표정 제어)
      actions        → FActionBatch.Actions        (실행할 액션 목록)
    """
    agent_id: str

    # BehaviorTree 서브트리 분기 기준. 기본값 "Common"으로 안전하게 폴백.
    behavior_mode: NPCBehaviorMode = "Common"

    # AnimBlueprint 표정 제어. 기본값 "Neutral".
    facial_state: NPCFacialState = "Neutral"

    actions: List[NPCAction]

    # AI 추론 근거 (디버깅/로깅용, UE5에서는 무시됨)
    reasoning: Optional[str] = None
