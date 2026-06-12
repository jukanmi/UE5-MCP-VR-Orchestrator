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


class ModeActionRequest(BaseModel):
    """C++ FModeActionRequest와 1:1 대응 (최상위 반환 객체)"""

    Mode: NPCBehaviorMode = "Common"
    ActionBatches: Dict[str, ActionBatch] = Field(default_factory=dict)
    # 재계획(replan) 시 산출된 NPC별 plan 회신 — npc_id → {goal, steps, relation_snapshot}.
    # e4b 단독(경량) 응답이면 비워둠. UE5 가 수신 시 NPCStateComponent::CurrentPlan 에 저장.
    NpcPlans: Dict[str, Dict] = Field(default_factory=dict)


class RejectResult(BaseModel):
    reason: str
    rejected: bool = True


WORLD_CONSTANTS = {
    "valid_npc_ids": ["Elara", "James", "Guard", "Merchant", "Blacksmith", "Player"],
    "valid_location_ids": ["TownSquare", "Tavern", "Forest", "Castle"],
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
    "Lifestyle": {"Sit", "Sleep", "Clean", "Read", "Pray", "Dance", "Sing"},
}
