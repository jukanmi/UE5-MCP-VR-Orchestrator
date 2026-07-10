from pydantic import BaseModel, Field
from typing import List, Literal, Dict

NPCBehaviorMode = Literal[
    "Combat",  # 전투 모드
    "Social",  # 사교 모드
    "Task",  # 상호작용/작업 모드
    "Investigation",  # 탐색/조사 모드
    "Lifestyle",  # 생활/대기 모드
    "Common",  # 공용/기본 액션 모드 (기본값)
]

NPCFacialState = Literal[
    "Neutral",
    "Happy",
    "Sad",
    "Angry",
    "Fear",
    "Surprised",
    "Disgusted",
    "Tired",
    "Pain",
]

EAction = Literal[
    "Idle",
    "Move",
    "Follow",
    "Wait",
    "Dialogue",
    "TurnTo",
    "Stop",
    "Scan",
    "UseItem",
    "Equip",
    "Unequip",
    "Attack",
    "Block",
    "Dodge",
    "Flee",
    "SignalAllies",
    "Trade",
    "Emote",
    "GiveItem",
    "Comfort",
    "HandObject",
    "PickUp",
    "Drop",
    "Craft",
    "Repair",
    "Investigate",
    "Track",
    "Scout",
    "Sit",
    "Sleep",
    "Read",
    "Pray",
    "Dance",
    "Sing",
]


class GameAction(BaseModel):
    """C++ FGameAction과 1:1 대응"""

    ActionType: EAction = "Idle"
    FacialState: NPCFacialState = "Neutral"
    Parameters: Dict[str, str] = Field(default_factory=dict)


class ActionBatch(BaseModel):
    """C++ FActionBatch와 1:1 대응"""

    AgentID: str
    Mode: NPCBehaviorMode = "Common"
    Actions: List[GameAction]


class DialogueActionItem(BaseModel):
    """Stage1 구조화 출력용 액션 항목. type 이 EAction Literal 이라
    Ollama structured output 이 34개 유효 액션만 생성 — 잘못된 Type 원천 차단.
    직렬화 시 빈 키는 [Action:] 태그에서 생략 (interface_output 매핑 기준)."""

    type: EAction = Field(description="Action to perform, e.g. Attack, Block, Move, GiveItem, Follow")
    target: str = Field(default="", description="Player, Self, Enemy or an NPC name; '' if none")
    item: str = Field(default="", description="Item name; '' if none")
    loc: str = Field(default="", description="Location id; '' if none")
    # 어휘 단일 소스는 C++ EMoveType(NPCActionTypes.h) — Walk/Run/Sprint/Crouch.
    # ParseMoveStyle 이 미매칭 값을 Walk 로 폴백하며 경고 로그를 남긴다.
    style: str = Field(default="", description="Modifier: Walk/Run/Sprint/Crouch for Move, emote name for Emote")


# DialogueActionItem 필드 ↔ 키 매핑 단일 소스 — (GameAction.Parameters 키, 필드명).
# 소비처 2곳: interface_output(Stage3 Parameters 매핑)·dialogue._serialize_dialogue(태그 키=필드명).
# 필드 추가/개명 시 여기 한 곳만 수정 (양쪽 하드코딩 2벌 유지하다 한쪽 누락되는 드리프트 방지).
DIALOGUE_ACTION_FIELD_MAP: tuple = (
    ("target_id", "target"),
    ("target_loc", "loc"),
    ("item", "item"),
    ("style", "style"),
)


class NPCPlanItem(BaseModel):
    """Stage2 12B plan 전용 구조화 출력 항목. 12B(abliterated)가 텍스트 포맷의
    '정제+plan 동시 출력' 지시를 무시하고 plan 라인만 출력해 대사가 증발하던 문제
    (2026-07 실측) 이후, Stage2 를 plan 산출 전용으로 축소 — grammar 강제로
    goal/steps 필드 누락·형식 위반 원천 차단."""

    npc_id: str = Field(description="NPC id copied EXACTLY from the '=== NPC: <id> ===' section header")
    goal: str = Field(description="Concrete outcome this NPC pursues over the next few turns, short Korean phrase")
    steps: List[str] = Field(description="2-4 concrete ordered action beats in Korean, no numbering prefix")


class PlanBatchResponse(BaseModel):
    """Stage2 plan-only 응답 루트. NPC 수만큼 NPCPlanItem."""

    npcs: List[NPCPlanItem]


class DialogueResponse(BaseModel):
    """Stage1 e4b 구조화 출력 스키마. speech·actions 를 required(default 없음)로 둬
    Ollama JSON 문법이 두 키를 강제 생성 — all-optional 이면 e4b 가 mode/facial 만
    내고 빠져나가 speech·actions 가 비는 문제(실측 확인) 방지. 직렬화 후 기존
    [Mode:][Facial:]"speech"[Action:] 텍스트로 재생 → 다운스트림 무변경."""

    mode: NPCBehaviorMode = Field(description="Behavior mode for this turn")
    facial: NPCFacialState = Field(description="Facial expression")
    speech: str = Field(description="What the NPC says out loud, 1-3 sentences, in character; never empty")
    tone: str = Field(default="", description="Emotional tone of speech, e.g. furiously")
    actions: List[DialogueActionItem] = Field(
        description="Game actions the NPC performs now. ACT (Attack/Block/Dodge/Move/...) "
        "when the situation calls for it; empty list ONLY if purely talking."
    )
    plan_achieved: bool = Field(
        default=False,
        description="True if the NPC's current plan goal has been achieved or completed based on this dialogue turn. "
        "Check the [Plan: goal=...] in context; set True only when the goal is clearly fulfilled.",
    )


class ModeActionRequest(BaseModel):
    """C++ FModeActionRequest와 1:1 대응 (최상위 반환 객체)"""

    Mode: NPCBehaviorMode = "Common"
    ActionBatches: Dict[str, ActionBatch] = Field(default_factory=dict)
    # 재계획(replan) 시 산출된 NPC별 plan 회신 — npc_id → {goal, steps, relation_snapshot}.
    # e4b 단독(경량) 응답이면 비워둠. UE5 가 수신 시 NPCStateComponent::CurrentPlan 에 저장.
    NpcPlans: Dict[str, Dict] = Field(default_factory=dict)
    # e4b Stage1 이 plan 달성 감지 시 per-NPC true. UE5 가 수신 시 FlagPlanAchieved() 호출.
    PlanAchieved: Dict[str, bool] = Field(default_factory=dict)


class RejectResult(BaseModel):
    reason: str
    rejected: bool = True


WORLD_CONSTANTS = {
    # Self/Enemy 는 C++ ResolveActionTarget(STTask_ExecuteSmartAction.cpp)이 런타임
    # 해석하는 센티넬 키워드(Self→자신, Enemy→BB perception 타겟). 구체 NPC ID 와 함께
    # 화이트리스트에 포함 — 누락 시 Attack/Block target=Enemy 등이 Rules 에서 제거됨.
    # ⚠️ 이 정적 목록은 폴백 — prompt 에 UE5 valid_targets(런타임 등록 NPC)가 오면
    # rules 검증은 그쪽을 우선. 여기는 personas/ 실존 NPC 와 동기 유지할 것.
    # (2026-07-09: 유령 항목 Merchant/Blacksmith 제거, 실존 Skadi/Moca 추가 —
    #  구 목록이 Skadi/Moca 타겟 액션을 조용히 제거하던 버그 수정)
    "valid_npc_ids": [
        "Elara",
        "James",
        "Skadi",
        "Moca",
        "Guard",
        "Player",
        "Self",
        "Enemy",
    ],
    # valid_location_ids 삭제됨(2026-07-09) — 소비처 0, 위치명 해석 미구현(POI 시스템 별도 spec 감).
    "WORLD_BOUNDS": {
        "x_min": -10000,
        "x_max": 10000,
        "y_min": -10000,
        "y_max": 10000,
        "z_min": -1000,
        "z_max": 2000,
    },
    "MAX_DAMAGE": 100,
    "MAX_SPEED": 600,
    "MAX_HEALTH": 100,
}

# BT 카테고리 분류맵
CATEGORY_ACTION_MAP = {
    "Common": {
        "Idle",
        "Move",
        "Follow",
        "Wait",
        "Dialogue",
        "TurnTo",
        "Stop",
        "Scan",
        "UseItem",
        "Equip",
        "Unequip",
    },
    "Combat": {"Attack", "Block", "Dodge", "Flee", "SignalAllies"},
    "Social": {"Trade", "Emote", "GiveItem", "Comfort", "HandObject"},
    "Task": {"PickUp", "Drop", "Craft", "Repair"},
    "Investigation": {"Investigate", "Track", "Scout"},
    # "Clean" 제거됨 — EAction Literal/C++ enum 에 없는 죽은 항목이었음
    "Lifestyle": {"Sit", "Sleep", "Read", "Pray", "Dance", "Sing"},
}

# 액션 → 카테고리 역조회 맵 (Rules 의 Mode 보정용)
ACTION_CATEGORY: Dict[str, str] = {action: cat for cat, actions in CATEGORY_ACTION_MAP.items() for action in actions}

# 액션별 필수 파라미터 — C++ ExecuteInteraction(NPCActionComponent.cpp) 동작 기준.
# 누락 시 C++ 가 무음 no-op 하거나(Follow/Attack/Track 등 if(!Target) return),
# 원점(0,0,0)으로 걸어가는(PickUp/Investigate BaseMove(ZeroVector)) 액션만 등재.
# 형식: 그룹 리스트 — 그룹 내 키는 OR(하나만 있으면 충족), 그룹 간은 AND.
#   예: Trade = [("target_id",), ("give_item_id", "item")]
#       → target_id 필수 AND (give_item_id 또는 item) 필수
# item 그룹에 target_id 포함 이유: C++ 가 item 비면 target_id 를 ItemID 로 폴백(레거시 호환).
ACTION_REQUIRED_PARAMS: Dict[str, list] = {
    "Dialogue": [("text",)],
    "Follow": [("target_id",)],
    "TurnTo": [("target_id", "target_loc")],
    "UseItem": [("item", "target_id")],
    "Equip": [("item", "target_id")],
    "Unequip": [("item", "target_id")],
    "Attack": [("target_id",)],
    "Trade": [("target_id",), ("give_item_id", "item")],
    "GiveItem": [("target_id",), ("item",)],
    "Comfort": [("target_id",)],
    "HandObject": [("item", "target_id")],
    "PickUp": [("target_loc",)],
    "Drop": [("item", "target_id")],
    "Craft": [("item_ids", "item", "target_id")],
    "Repair": [("item", "target_id")],
    "Investigate": [("target_loc",)],
    "Track": [("target_id",)],
    # target_loc 있으면 두 그룹 모두 충족, 없으면 start+end 둘 다 필요
    "Scout": [("target_loc", "start_location"), ("target_loc", "end_location")],
    # Idle/Move/Wait/Stop/Scan/Block/Dodge/Flee/SignalAllies/Emote/Lifestyle 류는
    # C++ 폴백이 견고(EQS/기본방향/무대상 허용)하므로 필수 없음 — 등재 금지.
}
