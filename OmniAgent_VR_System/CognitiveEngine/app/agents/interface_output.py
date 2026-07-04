"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: interface_output.py                                                   ║
║ Role: OUTPUT ADAPTER (Stage1 구조화 → UE5 ActionBatch)                      ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Transform Stage1 DialogueResponse into structured ActionBatch that UE5    ║
║   can execute. No regex — Stage1 grammar 가 이미 EAction/Mode/Facial        ║
║   Literal 을 검증했으므로 필드 매핑만 수행.                                  ║
║                                                                              ║
║ PIPELINE (Stage 3):                                                          ║
║   structured_responses Dict[npc_id, DialogueResponse]                        ║
║     → _structure_from_dialogue (필드 매핑) → action_batches                 ║
║   + Python 규칙: FacialState vs persona traits 교차 검증 (오염 보정)        ║
║                                                                              ║
║ INPUT:  structured_responses (Dict[str, DialogueResponse])                   ║
║ OUTPUT: action_batches (Dict[str, ActionBatch])                              ║
║                                                                              ║
║ 감정 parity: Dialogue 액션 emotion = facial (Neutral 이면 tone→emotion).     ║
║                                                                              ║
║ [C++ 연동 핵심]                                                               ║
║   - ActionType 은 C++ Enum(EAction) 과 1:1 대응 (Stage1 Literal 로 보장)     ║
║   - Parameters 키(target_id/target_loc/item/style)는 snake_case (§1 규칙)    ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""

import asyncio
from typing import Dict
from .state import AgentState
from .subgraphs.dialogue import load_persona
from ..schemas.actions import (
    ActionBatch,
    GameAction,
    DialogueResponse,
)


# trait → 금지 FacialState 매핑
# WHY: 멀티 NPC 단일 정제 시 감정 수렴 오염 발생 가능. 결정론적 Python 규칙으로 교정.
TRAIT_EMOTION_MAP: dict[str, set[str]] = {
    "Aggressive": {"Fear", "Sad"},
    "Cowardly": {"Angry"},
    "Loyal": {"Disgusted"},
    "Gentle": {"Angry", "Disgusted"},
    "Proud": {"Fear", "Sad"},
    "Cautious": set(),
    "Reserved": set(),
    "Observant": set(),
}

# trait → 금지 감정 발동 시 대체 FacialState
TRAIT_DEFAULT_FACIAL: dict[str, str] = {
    "Aggressive": "Angry",
    "Cowardly": "Fear",
    "Loyal": "Neutral",
    "Gentle": "Neutral",
    "Proud": "Neutral",
}


def _correct_facial_contamination(action: GameAction, persona_traits: list[str]) -> GameAction:
    """
    FacialState가 persona traits와 모순이면 결정론적으로 교체.
    e.g. Aggressive NPC 가 Fear 로 수렴 → Angry 로 복원.
    """
    current = action.FacialState
    for trait in persona_traits:
        forbidden = TRAIT_EMOTION_MAP.get(trait, set())
        if current in forbidden:
            replacement = TRAIT_DEFAULT_FACIAL.get(trait, "Neutral")
            print(f"[Interface Output] 오염 보정: {current} → {replacement} (trait={trait})")
            action.FacialState = replacement
            break
    return action


# 자연어 감정 키워드(한/영) → 정규 FacialState. 삽입 순서 = 매칭 우선순위.
# tone(예: "furiously") → emotion 변환용 (facial=Neutral 일 때만 사용).
_EMOTION_KEYWORD_MAP = {
    "기쁘": "Happy",
    "행복": "Happy",
    "happy": "Happy",
    "cheerful": "Happy",
    "joy": "Happy",
    "슬프": "Sad",
    "우울": "Sad",
    "sad": "Sad",
    "화나": "Angry",
    "분노": "Angry",
    "angry": "Angry",
    "furious": "Angry",
    "무서": "Fear",
    "fear": "Fear",
    "scared": "Fear",
    "놀라": "Surprised",
    "surprised": "Surprised",
    "alarm": "Surprised",
    "역겨": "Disgusted",
    "disgust": "Disgusted",
    "피곤": "Tired",
    "exhaust": "Tired",
    "tired": "Tired",
    "아프": "Pain",
    "pain": "Pain",
}


def _normalize_emotion(emotion_text: str) -> str:
    emotion_lower = (emotion_text or "").lower()
    for keyword, emotion in _EMOTION_KEYWORD_MAP.items():
        if keyword in emotion_lower:
            return emotion
    return "Neutral"


# DialogueActionItem 필드 → GameAction.Parameters 키 (snake_case, §1 JSON 키 규칙).
# 빈 값은 생략 — C++ ExecuteInteraction 이 필수 파라미터 누락 시 견고 폴백 or Rules 제거.
_DIALOGUE_PARAM_MAP = (
    ("target_id", "target"),
    ("target_loc", "loc"),
    ("item", "item"),
    ("style", "style"),
)


def _create_empty_batch(npc_id: str) -> ActionBatch:
    return ActionBatch(
        AgentID=npc_id,
        Mode="Common",
        Actions=[
            GameAction(
                ActionType="Dialogue",
                FacialState="Neutral",
                Parameters={"text": "...", "emotion": "Neutral"},
            )
        ],
    )


def _structure_from_dialogue(npc_id: str, resp: DialogueResponse, persona_traits: list[str]) -> ActionBatch:
    """
    Stage 3: DialogueResponse → ActionBatch. 정규식 없음 — Stage1 Literal 검증 활용.
    Dialogue 액션(speech) + resp.actions 필드 매핑 + FacialState 오염 보정.
    """
    facial = resp.facial
    # 텍스트 경로 parity: facial 우선, Neutral 이면 tone→emotion 정규화.
    emotion = facial if facial != "Neutral" else _normalize_emotion(resp.tone)
    speech = (resp.speech or "").strip() or "..."

    actions = [
        GameAction(
            ActionType="Dialogue",
            FacialState=emotion,
            Parameters={"text": speech[:200], "emotion": emotion},
        )
    ]

    for act in resp.actions:
        params: Dict[str, str] = {}
        field_vals = {"target": act.target, "loc": act.loc, "item": act.item, "style": act.style}
        for dst_key, src_field in _DIALOGUE_PARAM_MAP:
            v = (field_vals[src_field] or "").strip()
            if v:
                params[dst_key] = v
        actions.append(GameAction(ActionType=act.type, FacialState=facial, Parameters=params))

    batch = ActionBatch(AgentID=npc_id, Mode=resp.mode, Actions=actions)

    # FacialState 오염 보정 (모든 액션에 적용)
    if persona_traits:
        for action in batch.Actions:
            _correct_facial_contamination(action, persona_traits)

    return batch


async def interface_output_node(state: AgentState):
    """
    Interface Output Agent (Stage 3, async).

    structured_responses Dict[npc_id, DialogueResponse] → 필드 매핑(CPU, to_thread) → action_batches.
    LLM·정규식 아님 — Stage1 구조화 출력을 규칙 기반으로 ActionBatch 변환.
    구조화 없으면(방어적, 예: supervisor 폴백 경로) empty batch.
    """
    structured: Dict[str, DialogueResponse] = state.get("structured_responses") or {}

    # 방어 경로: 구조화 출력 없음 → empty batch (raw_response 재파싱 안 함).
    if not structured:
        print("[Interface Output] WARNING: structured_responses 없음, empty batch")
        npc_id = state.get("target_npc", "Elara")
        single_batch = _create_empty_batch(npc_id)
        return {
            "action_batches": {npc_id: single_batch},
            "action_batch": single_batch,
            "current_speaker": "Interface_Output",
            "next": "Rules",
        }

    # 각 NPC의 persona traits 로드 (동기 파일 I/O — 이벤트 루프 블로킹 방지 위해 오프로드)
    traits_map: Dict[str, list[str]] = {}
    for npc_id in structured:
        persona = await asyncio.to_thread(load_persona, npc_id) or {}
        traits_map[npc_id] = persona.get("traits", [])

    # Stage 3: 병렬 필드 매핑 (경량이나 오염보정 포함 — to_thread 로 루프 비블로킹)
    async def _structure_async(npc_id: str, resp: DialogueResponse) -> tuple[str, ActionBatch]:
        batch = await asyncio.to_thread(_structure_from_dialogue, npc_id, resp, traits_map.get(npc_id, []))
        return npc_id, batch

    results = await asyncio.gather(*[_structure_async(npc_id, resp) for npc_id, resp in structured.items()])

    action_batches: Dict[str, ActionBatch] = {npc_id: batch for npc_id, batch in results}
    print(f"[Interface Output] 구조화 완료: {list(action_batches.keys())}")

    # 단일 NPC 호환: action_batch 도 채움
    first_batch = next(iter(action_batches.values()), None)

    return {
        "action_batches": action_batches,
        "action_batch": first_batch,
        "current_speaker": "Interface_Output",
        "next": "Rules",
    }
