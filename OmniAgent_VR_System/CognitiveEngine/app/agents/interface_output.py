"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: interface_output.py                                                   ║
║ Role: OUTPUT ADAPTER (LLM → UE5)                                            ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Transform free-form NPC response into structured ActionBatch that UE5     ║
║   can execute. Parses natural language into game engine function calls.     ║
║                                                                              ║
║ INPUT:  raw_response (str)                                                   ║
║         - [Mode: X] [Facial: Y] 태그 + "Speech" + *actions* + (emotions)    ║
║ OUTPUT: ActionBatch                                                          ║
║         - behavior_mode, facial_state, actions[NPCAction, ...]              ║
║                                                                              ║
║ 구조화 전략:                                                                  ║
║   1단계: [Mode: X] [Facial: Y] 파싱 → behavior_mode, facial_state 결정     ║
║   2단계: Ollama(로컬)로 action 구조화 시도                                      ║
║   3단계: 실패 시 regex 기반 폴백 파서 사용                                    ║
║   4단계: action_category 자동 추론 (CATEGORY_ACTION_MAP 역참조)              ║
║                                                                              ║
║ [C++ 연동 핵심]                                                               ║
║   - action_type은 C++ Enum(ECommonAction, ECombatAction 등)과 1:1 대응      ║
║   - action_category는 BT 서브트리 분기 기준                                  ║
║   - 불일치 시 Pydantic model_validator가 ValidationError 발생                ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import json
import re
from .state import AgentState
from ..schemas.actions import (
    ActionBatch, GameAction,
    CATEGORY_ACTION_MAP,
)



# ─────────────────────────────────────────────────────────────────────────────
# 유효성 검증용 상수
# ─────────────────────────────────────────────────────────────────────────────
VALID_MOVE_STYLES = {"Walk", "Run", "Sprint", "Crouch", "Crawl"}


def _parse_mode_and_facial(raw_response: str) -> tuple[str, str, str, bool]:
    """
    raw_response에서 [Mode: X] [Facial: Y] 태그를 파싱하고 제거.

    Returns:
        (behavior_mode, facial_state, cleaned_response, parse_ok)
        parse_ok=False이면 태그를 찾지 못해 기본값으로 fallback됨
    """
    mode = "Common"
    facial = "Neutral"

    mode_match = re.search(r'\[Mode:\s*(\w+)\]', raw_response, re.IGNORECASE)
    facial_match = re.search(r'\[Facial:\s*(\w+)\]', raw_response, re.IGNORECASE)

    VALID_MODES = {"Combat", "Social", "Task", "Investigation", "Lifestyle", "Common"}
    VALID_FACIALS = {"Neutral", "Happy", "Sad", "Angry", "Fear", "Surprised", "Disgusted", "Tired", "Pain"}

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

    # 태그 라인 제거
    cleaned = re.sub(
        r'\[Mode:\s*\w+\]\s*\[Facial:\s*\w+\]\s*\n?', '',
        raw_response, flags=re.IGNORECASE
    ).strip()

    return mode, facial, cleaned, (mode_ok and facial_ok)


def interface_output_node(state: AgentState):
    """
    Interface Output Agent.

    raw_response에서 [Mode: X][Facial: Y] 파싱 + action 구조화를 모두 수행.

    Input: AgentState with raw_response (str) and target_npc (str)
    Output: AgentState with action_batch (ActionBatch)
    """
    raw_response = state.get("raw_response", "")
    npc_id = state.get("target_npc", "Elara")

    if not raw_response:
        print("[Interface Output] WARNING: No raw_response found")
        return {
            "action_batch": _create_empty_batch(npc_id),
            "current_speaker": "Interface_Output",
            "next": "Rules"
        }

    # 1단계: [Mode: X] [Facial: Y] 파싱 및 제거
    behavior_mode, facial_state, clean_response, parse_ok = _parse_mode_and_facial(raw_response)
    if not parse_ok:
        print(f"[Interface Output] WARNING: Mode/Facial 태그 파싱 실패 — fallback 사용 (Mode={behavior_mode}, Facial={facial_state}): {raw_response[:80]!r}")
    else:
        print(f"[Interface Output] Parsed Mode={behavior_mode}, Facial={facial_state}")
    print(f"[Interface Output] Structuring: '{clean_response[:80]}...'")

    # 2단계: [Action:] 구조화 태그 파싱(원문 기준 — clean_response 는 [Mode]/[Facial] 만 제거).
    tag_actions = _parse_action_tags(raw_response, facial_state)
    if tag_actions:
        print(f"[Interface Output] [Action:] 태그 {len(tag_actions)}개 파싱")

    # 3단계: Dialogue + 액션 배치 (태그 우선, 없으면 asterisk 폴백)
    action_batch = _regex_fallback_parse(clean_response, npc_id, behavior_mode, facial_state, tag_actions)

    if action_batch and action_batch.Actions:
        print(f"[Interface Output] Regex 파싱 성공: {len(action_batch.Actions)}개 액션")
    else:
        # 형식 완전 이탈 시 전체 텍스트를 Dialogue로 처리
        print("[Interface Output] 액션 없음, 전체 텍스트를 Dialogue로 처리")
        action_batch = _create_empty_batch(npc_id)
        action_batch.Actions[0].Parameters["text"] = clean_response[:200]
        action_batch.Actions[0].FacialState = facial_state  # 태그값 유지(중립 강제 X)

    return {
        "action_batch": action_batch,
        "current_speaker": "Interface_Output",
        "next": "Rules"
    }



# C++ EAction(NPCActionTypes.h) 34종 — LLM [Action:] Type 검증용 (canonical 케이스).
VALID_ACTIONS = {
    "Idle", "Move", "Follow", "Wait", "Dialogue", "TurnTo", "Stop", "Scan",
    "UseItem", "Equip", "Unequip",
    "Attack", "Block", "Dodge", "Flee", "SignalAllies",
    "Trade", "Emote", "GiveItem", "Comfort", "HandObject",
    "PickUp", "Drop", "Craft", "Repair",
    "Investigate", "Track", "Scout",
    "Sit", "Sleep", "Read", "Pray", "Dance", "Sing",
}
_VALID_ACTIONS_LOWER = {a.lower(): a for a in VALID_ACTIONS}

# 태그 키 → Parameters 키 (CLAUDE.md §1: Parameters 는 snake_case, NPCActionKeys 정합).
_PARAM_KEY_MAP = {
    "target": "target_id", "item": "item", "loc": "target_loc",
    "location": "target_loc", "style": "style", "direction": "direction",
    "duration": "duration",
}


def _parse_action_tags(raw_response: str, facial_state: str) -> list:
    """[Action: <Type> k=v ...] 태그들을 GameAction 리스트로.

    target 등 의미키워드(Player/Self/Enemy/<NpcName>) 는 그대로 Parameters 에 담고,
    실제 AgentID/아이템 해석은 C++(perception/inventory) 책임.
    """
    from ..schemas.actions import GameAction
    out = []
    for m in re.finditer(r'\[Action:\s*([^\]]+)\]', raw_response, re.IGNORECASE):
        body = m.group(1).strip()
        tmatch = re.match(r'([A-Za-z]+)', body)
        if not tmatch:
            continue
        canon = _VALID_ACTIONS_LOWER.get(tmatch.group(1).lower())
        if not canon:
            print(f"[Interface Output] 알 수 없는 Action Type 무시: {tmatch.group(1)!r}")
            continue
        params = {}
        for k, v in re.findall(r'(\w+)\s*=\s*("[^"]*"|\S+)', body):
            key = _PARAM_KEY_MAP.get(k.lower())
            val = v.strip('"').strip()
            if key and val:
                params[key] = val
        out.append(GameAction(ActionType=canon, FacialState=facial_state, Parameters=params))
    return out


def _regex_fallback_parse(raw_response: str, npc_id: str,
                          behavior_mode: str = "Common",
                          facial_state: str = "Neutral",
                          tag_actions: list = None) -> ActionBatch:
    """
    raw_response 에서 Dialogue("quotes") + 게임 액션을 추출.
    액션 소스 우선순위:
      ① [Action:] 구조화 태그(tag_actions) — 1순위. 있으면 asterisk 키워드 매핑 생략.
      ② 폴백: *asterisk* 자연어 키워드 매핑 (태그를 안 쓴 모델 대비).
    """
    from ..schemas.actions import GameAction
    actions = []

    # 1. 대사 추출 ("quotes" → Dialogue 액션)
    speech_matches = re.findall(r'"([^"]+)"', raw_response)
    if speech_matches:
        # FacialState 소스 우선순위:
        #   ① [Facial: X] 태그(facial_state) — 프롬프트가 강제, 9종 검증됨(1순위)
        #   ② 괄호 톤워드 (furiously) — 태그가 Neutral/누락일 때만 폴백
        # WHY: 태그가 더 신뢰. 자유 톤워드(coldly/menacingly 등)는 매핑 실패해
        #      Neutral 로 죽으므로, 검증된 태그를 우선해 감정 음색을 살린다.
        emotion_matches = re.findall(r'\(([^)]+)\)', raw_response)
        paren_emotion = _normalize_emotion(emotion_matches[0]) if emotion_matches else "Neutral"
        emotion = facial_state if facial_state != "Neutral" else paren_emotion

        actions.append(GameAction(
            ActionType="Dialogue",
            FacialState=emotion,
            Parameters={"text": speech_matches[0], "emotion": emotion},
        ))

    # 2. 게임 액션 — [Action:] 태그 1순위, 없으면 asterisk 키워드 폴백
    if tag_actions:
        actions.extend(tag_actions)
    else:
        action_matches = re.findall(r'\*([^*]+)\*', raw_response)
        for action_text in action_matches:
            parsed = _parse_natural_action(action_text, behavior_mode)
            if parsed:
                actions.append(parsed)

    # 3. 아무 액션도 없으면 전체를 Dialogue로 처리 — FacialState 는 태그값 유지
    if not actions:
        clean_text = re.sub(r'[*()]', '', raw_response).strip()
        if clean_text:
            actions.append(GameAction(
                ActionType="Dialogue",
                FacialState=facial_state,
                Parameters={"text": clean_text[:200], "emotion": facial_state},
            ))

    return ActionBatch(
        AgentID=npc_id,
        Mode=behavior_mode,
        Actions=actions
    )


def _parse_natural_action(text: str, behavior_mode: str = "Common"):
    """
    단일 자연어 액션 → GameAction 변환.

    키워드 매칭 테이블 순서가 중요: 더 구체적인 패턴을 먼저 배치.
    """
    from ..schemas.actions import GameAction
    text_lower = text.lower()

    # (keywords, action_type, target_id, parameters) 매핑 테이블
    KEYWORD_ACTION_MAP = [
        # Combat
        (["공격", "attack", "strike", "hit", "swing", "베", "때", "slash"],
         "Attack",  "Enemy",  {}),
        (["방어", "막", "block", "shield", "parry", "defend"],
         "Block",   None,     {"duration": "2.0"}),
        (["구르", "회피", "dodge", "evade", "roll"],
         "Dodge",   None,     {"direction": "Back"}),
        (["도망", "달아", "flee", "escape", "run away"],
         "Flee",    None,     {}),
        # Movement (구체적 → 범용 순서)
        (["뛰어", "달려", "run", "rush", "sprint", "빠르게"],
         "Move",    "Player", {"style": "Run"}),
        (["걸어", "천천히", "walk", "slowly", "다가"],
         "Move",    "Player", {"style": "Walk"}),
        (["기어", "crawl", "sneak"],
         "Move",    "Player", {"style": "Crawl"}),
        # Task
        (["줍", "집", "pick", "grab", "take"],
         "PickUp",  None,     {}),
        (["버리", "drop", "discard"],
         "Drop",    None,     {}),
        # Investigation
        (["수색", "조사", "investigate", "search", "examine"],
         "Investigate", None, {}),
        (["추적", "track", "follow trail"],
         "Track",   None,     {}),
        (["두리번", "둘러", "scan", "look around"],
         "Scan",    None,     {}),
        # Lifestyle
        (["앉", "sit", "sits"],
         "Sit",     None,     {}),
        (["잠", "자", "sleep", "rest", "lie down"],
         "Sleep",   None,     {}),
        (["읽", "read"],
         "Read",    None,     {}),
        # Common
        (["멈", "기다", "wait", "stop", "pause"],
         "Wait",    None,     {"duration": "3.0"}),
        (["바라", "돌아", "turn", "face", "look at"],
         "TurnTo",  "Player", {}),
    ]

    for keywords, action_type, target_id, parameters in KEYWORD_ACTION_MAP:
        if any(word in text_lower for word in keywords):
            params = parameters.copy()
            if target_id:
                params["target_id"] = target_id
            
            # String 변환
            str_params = {k: str(v) for k, v in params.items()}

            return GameAction(
                ActionType=action_type,
                FacialState="Neutral",
                Parameters=str_params,
            )

    # Emote (세부 분기가 필요해서 별도 처리)
    if any(word in text_lower for word in ["웃", "smile", "laugh", "nod", "bow", "wave", "손"]):
        if "bow" in text_lower or "인사" in text_lower:
            gesture = "Bow"
        elif "wave" in text_lower or "손" in text_lower:
            gesture = "Wave"
        elif "nod" in text_lower or "끄덕" in text_lower:
            gesture = "Nod"
        else:
            gesture = "Smile"
        return GameAction(
            ActionType="Emote",
            FacialState="Neutral",
            Parameters={"gesture": gesture},
        )

    return None


def _normalize_emotion(emotion_text: str) -> str:
    """감정 텍스트를 C++ EFacialState Enum에 대응하는 표준값으로 정규화."""
    emotion_lower = emotion_text.lower()

    # 왜 이 매핑이 필요한가: LLM은 "기쁘게", "cheerfully" 등 자유 텍스트를 출력하지만
    # C++ AnimBlueprint는 "Happy", "Angry" 등 정확한 Enum 문자열만 인식함.
    EMOTION_MAP = {
        "기쁘": "Happy", "행복": "Happy", "happy": "Happy",
        "cheerful": "Happy", "joy": "Happy",
        "슬프": "Sad", "우울": "Sad", "sad": "Sad",
        "화나": "Angry", "분노": "Angry", "angry": "Angry",
        "furious": "Angry",
        "무서": "Fear", "fear": "Fear", "scared": "Fear",
        "놀라": "Surprised", "surprised": "Surprised", "alarm": "Surprised",
        "역겨": "Disgusted", "disgust": "Disgusted",
        "피곤": "Tired", "exhaust": "Tired", "tired": "Tired",
        "아프": "Pain", "pain": "Pain",
    }

    for keyword, emotion in EMOTION_MAP.items():
        if keyword in emotion_lower:
            return emotion

    return "Neutral"


def _create_empty_batch(npc_id: str) -> ActionBatch:
    """폴백용 빈 ActionBatch 생성."""
    from ..schemas.actions import ActionBatch, GameAction
    return ActionBatch(
        AgentID=npc_id,
        Mode="Common",
        Actions=[GameAction(
            ActionType="Dialogue",
            FacialState="Neutral",
            Parameters={"text": "...", "emotion": "Neutral"},
        )]
    )
