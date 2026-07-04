"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: interface_output.py                                                   ║
║ Role: OUTPUT ADAPTER (LLM → UE5)                                            ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Transform free-form NPC response into structured ActionBatch that UE5     ║
║   can execute. Parses natural language into game engine function calls.     ║
║                                                                              ║
║ PIPELINE (Stage 3):                                                          ║
║   raw_responses Dict[npc_id, str] → 정규식 N개 병렬 파싱 → action_batches ║
║   + Python 규칙: FacialState vs persona traits 교차 검증 (오염 보정)        ║
║                                                                              ║
║ INPUT:  raw_responses (Dict[str, str])  npc_id → refined text               ║
║         - [Mode: X] [Facial: Y] 태그 + "Speech" + [Action:] tags            ║
║ OUTPUT: action_batches (Dict[str, ActionBatch])                              ║
║                                                                              ║
║ 구조화 전략:                                                                  ║
║   1단계: [Mode: X] [Facial: Y] 파싱 → behavior_mode, facial_state           ║
║   2단계: [Action:] 구조화 태그 파싱 (1순위)                                  ║
║   3단계: 실패 시 regex 기반 폴백 파서                                        ║
║   4단계: Python TRAIT_EMOTION_MAP으로 FacialState 오염 보정                 ║
║                                                                              ║
║ [C++ 연동 핵심]                                                               ║
║   - action_type은 C++ Enum(ECommonAction, ECombatAction 등)과 1:1 대응      ║
║   - action_category는 BT 서브트리 분기 기준                                  ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""

import re
import asyncio
from typing import Dict, Optional
from .state import AgentState
from .subgraphs.dialogue import load_persona
from ..schemas.actions import (
    ActionBatch,
    GameAction,
)


VALID_MOVE_STYLES = {"Walk", "Run", "Sprint", "Crouch", "Crawl"}

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


VALID_MODES = {"Combat", "Social", "Task", "Investigation", "Lifestyle", "Common"}
VALID_FACIALS = {"Neutral", "Happy", "Sad", "Angry", "Fear", "Surprised", "Disgusted", "Tired", "Pain"}


def _parse_mode_and_facial(raw_response: str) -> tuple[str, str, str, bool]:
    mode = "Common"
    facial = "Neutral"

    mode_match = re.search(r"\[Mode:\s*(\w+)\]", raw_response, re.IGNORECASE)
    facial_match = re.search(r"\[Facial:\s*(\w+)\]", raw_response, re.IGNORECASE)

    mode_ok = False
    facial_ok = False

    if mode_match:
        parsed_mode = mode_match.group(1).capitalize()
        if parsed_mode in VALID_MODES:
            mode = parsed_mode
            mode_ok = True

    if facial_match:
        parsed_facial = facial_match.group(1).capitalize()
        if parsed_facial in VALID_FACIALS:
            facial = parsed_facial
            facial_ok = True

    cleaned = re.sub(
        r"\[Mode:\s*\w+\]\s*\[Facial:\s*\w+\]\s*\n?",
        "",
        raw_response,
        flags=re.IGNORECASE,
    ).strip()

    return mode, facial, cleaned, (mode_ok and facial_ok)


VALID_ACTIONS = {
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
}
_VALID_ACTIONS_LOWER = {a.lower(): a for a in VALID_ACTIONS}

_PARAM_KEY_MAP = {
    "target": "target_id",
    "item": "item",
    "loc": "target_loc",
    "location": "target_loc",
    "style": "style",
    "direction": "direction",
    "duration": "duration",
}


def _parse_action_tags(raw_response: str, facial_state: str) -> list:
    out = []
    for m in re.finditer(r"\[Action:\s*([^\]]+)\]", raw_response, re.IGNORECASE):
        body = m.group(1).strip()
        tmatch = re.match(r"([A-Za-z]+)", body)
        if not tmatch:
            continue
        canon = _VALID_ACTIONS_LOWER.get(tmatch.group(1).lower())
        if not canon:
            print(f"[Interface Output] 알 수 없는 Action Type 무시: {tmatch.group(1)!r}")
            continue
        params = {}
        for k, v in re.findall(r'(\w+)\s*=\s*("[^"]*"|\S+)', body):
            key = _PARAM_KEY_MAP.get(k.lower(), k.lower())
            val = v.strip('"').strip()
            if key and val:
                params[key] = val
        out.append(GameAction(ActionType=canon, FacialState=facial_state, Parameters=params))
    return out


# 자연어 감정 키워드(한/영) → 정규 FacialState. 삽입 순서 = 매칭 우선순위.
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
    emotion_lower = emotion_text.lower()
    for keyword, emotion in _EMOTION_KEYWORD_MAP.items():
        if keyword in emotion_lower:
            return emotion
    return "Neutral"


# 자연어 키워드(한/영) → (action_type, 기본 target_id, 기본 parameters). 순서 = 매칭 우선순위.
_KEYWORD_ACTION_MAP = [
    (
        ["공격", "attack", "strike", "hit", "swing", "베", "때", "slash"],
        "Attack",
        "Enemy",
        {},
    ),
    (
        ["방어", "막", "block", "shield", "parry", "defend"],
        "Block",
        None,
        {"duration": "2.0"},
    ),
    (
        ["구르", "회피", "dodge", "evade", "roll"],
        "Dodge",
        None,
        {"direction": "Back"},
    ),
    (["도망", "달아", "flee", "escape", "run away"], "Flee", None, {}),
    (
        ["뛰어", "달려", "run", "rush", "sprint", "빠르게"],
        "Move",
        "Player",
        {"style": "Run"},
    ),
    (
        ["걸어", "천천히", "walk", "slowly", "다가"],
        "Move",
        "Player",
        {"style": "Walk"},
    ),
    (["기어", "crawl", "sneak"], "Move", "Player", {"style": "Crawl"}),
    (["줍", "집", "pick", "grab", "take"], "PickUp", None, {}),
    (["버리", "drop", "discard"], "Drop", None, {}),
    (["수색", "조사", "investigate", "search", "examine"], "Investigate", None, {}),
    (["추적", "track", "follow trail"], "Track", None, {}),
    (["두리번", "둘러", "scan", "look around"], "Scan", None, {}),
    (["앉", "sit", "sits"], "Sit", None, {}),
    (["잠", "자", "sleep", "rest", "lie down"], "Sleep", None, {}),
    (["읽", "read"], "Read", None, {}),
    (["멈", "기다", "wait", "stop", "pause"], "Wait", None, {"duration": "3.0"}),
    (["바라", "돌아", "turn", "face", "look at"], "TurnTo", "Player", {}),
]


# Emote 제스처 감지 — 트리거 키워드 중 하나라도 있으면 Emote. 세부 gesture 는
# _GESTURE_KEYWORD_MAP 순서(우선순위)로 판정, 미매칭이면 Smile 기본.
_GESTURE_TRIGGER = ["웃", "smile", "laugh", "nod", "bow", "wave", "손", "끄덕", "인사"]
_GESTURE_KEYWORD_MAP = [
    (["bow", "인사"], "Bow"),
    (["wave", "손"], "Wave"),
    (["nod", "끄덕"], "Nod"),
]


def _parse_natural_action(text: str, behavior_mode: str = "Common") -> Optional[GameAction]:
    text_lower = text.lower()
    for keywords, action_type, target_id, parameters in _KEYWORD_ACTION_MAP:
        if any(word in text_lower for word in keywords):
            params = parameters.copy()
            if target_id:
                params["target_id"] = target_id
            return GameAction(
                ActionType=action_type,
                FacialState="Neutral",
                Parameters={k: str(v) for k, v in params.items()},
            )

    if any(word in text_lower for word in _GESTURE_TRIGGER):
        gesture = "Smile"
        for keywords, g in _GESTURE_KEYWORD_MAP:
            if any(word in text_lower for word in keywords):
                gesture = g
                break
        return GameAction(ActionType="Emote", FacialState="Neutral", Parameters={"gesture": gesture})

    return None


def _strip_to_dialogue(raw_response: str, facial_state: str) -> Optional[GameAction]:
    """[Action:] 태그·`*()` 제거 후 남은 텍스트를 Dialogue 액션으로(없으면 None). 200자 절단."""
    bare = re.sub(r"\[Action:\s*[^\]]+\]", "", raw_response, flags=re.IGNORECASE)
    bare = re.sub(r"[*()]", "", bare).strip()
    if not bare:
        return None
    return GameAction(
        ActionType="Dialogue",
        FacialState=facial_state,
        Parameters={"text": bare[:200], "emotion": facial_state},
    )


def _regex_fallback_parse(
    raw_response: str,
    npc_id: str,
    behavior_mode: str = "Common",
    facial_state: str = "Neutral",
    tag_actions: list = None,
) -> ActionBatch:
    actions = []

    speech_matches = re.findall(r'"([^"]+)"', raw_response)
    if speech_matches:
        emotion_matches = re.findall(r"\(([^)]+)\)", raw_response)
        paren_emotion = _normalize_emotion(emotion_matches[0]) if emotion_matches else "Neutral"
        emotion = facial_state if facial_state != "Neutral" else paren_emotion
        actions.append(
            GameAction(
                ActionType="Dialogue",
                FacialState=emotion,
                Parameters={"text": speech_matches[0], "emotion": emotion},
            )
        )
    else:
        act = _strip_to_dialogue(raw_response, facial_state)
        if act:
            actions.append(act)

    if tag_actions:
        actions.extend(tag_actions)
    else:
        action_matches = re.findall(r"\*([^*]+)\*", raw_response)
        for action_text in action_matches:
            parsed = _parse_natural_action(action_text, behavior_mode)
            if parsed:
                actions.append(parsed)

    if not actions:
        act = _strip_to_dialogue(raw_response, facial_state)
        if act:
            actions.append(act)

    return ActionBatch(AgentID=npc_id, Mode=behavior_mode, Actions=actions)


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


def _structure_single(npc_id: str, refined_text: str, persona_traits: list[str]) -> ActionBatch:
    """
    Stage 3: 단일 NPC refined_text → ActionBatch.
    regex 파싱 + Python FacialState 오염 보정.
    """
    if not refined_text:
        return _create_empty_batch(npc_id)

    behavior_mode, facial_state, clean_response, parse_ok = _parse_mode_and_facial(refined_text)
    if not parse_ok:
        print(f"[Interface Output] Mode/Facial 태그 누락 ({npc_id}), fallback 사용")

    tag_actions = _parse_action_tags(refined_text, facial_state)

    batch = _regex_fallback_parse(clean_response, npc_id, behavior_mode, facial_state, tag_actions)

    if not batch.Actions:
        batch = _create_empty_batch(npc_id)
        batch.Actions[0].Parameters["text"] = clean_response[:200]
        batch.Actions[0].FacialState = facial_state

    # FacialState 오염 보정 (모든 액션에 적용)
    if persona_traits:
        for action in batch.Actions:
            _correct_facial_contamination(action, persona_traits)

    return batch


async def interface_output_node(state: AgentState):
    """
    Interface Output Agent (Stage 3, async).

    raw_responses Dict[npc_id, str] → 정규식 N개 병렬 파싱(CPU, to_thread) → action_batches.
    LLM 아님 — Stage1 텍스트를 규칙 기반으로 ActionBatch 구조화.
    단일 NPC 호환: raw_responses 없으면 raw_response + target_npc 폴백.
    """
    raw_responses: Dict[str, str] = state.get("raw_responses") or {}

    # 단일 NPC 호환 경로
    if not raw_responses:
        npc_id = state.get("target_npc", "Elara")
        raw_response = state.get("raw_response", "")
        if not raw_response:
            print("[Interface Output] WARNING: raw_response 없음")
            single_batch = _create_empty_batch(npc_id)
            return {
                "action_batches": {npc_id: single_batch},
                "action_batch": single_batch,
                "current_speaker": "Interface_Output",
                "next": "Rules",
            }
        raw_responses = {npc_id: raw_response}

    # 각 NPC의 persona traits 로드 (동기, 빠름)
    traits_map: Dict[str, list[str]] = {}
    for npc_id in raw_responses:
        # 동기 파일 I/O — 이벤트 루프 블로킹 방지 위해 스레드 오프로드.
        persona = await asyncio.to_thread(load_persona, npc_id) or {}
        traits_map[npc_id] = persona.get("traits", [])

    # Stage 3: 병렬 구조화 (CPU-bound이므로 to_thread 사용)
    async def _structure_async(npc_id: str, text: str) -> tuple[str, ActionBatch]:
        batch = await asyncio.to_thread(_structure_single, npc_id, text, traits_map.get(npc_id, []))
        return npc_id, batch

    results = await asyncio.gather(*[_structure_async(npc_id, text) for npc_id, text in raw_responses.items()])

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
