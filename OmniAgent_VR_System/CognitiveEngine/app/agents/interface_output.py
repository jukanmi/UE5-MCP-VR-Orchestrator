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
║   2단계: Gemini CLI로 action 구조화 시도                                      ║
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
from ..utils.llm_factory import call_gemini_cli


# ─────────────────────────────────────────────────────────────────────────────
# action_type → action_category 역참조 테이블
# 왜: LLM이 action_type만 출력하면, 어떤 카테고리(서브트리)에 속하는지 자동 추론.
# CATEGORY_ACTION_MAP을 뒤집어서 생성. 중복(Follow 등)은 첫 매칭 우선.
# ─────────────────────────────────────────────────────────────────────────────
ACTION_TO_CATEGORY: dict[str, str] = {}
for category, actions in CATEGORY_ACTION_MAP.items():
    for action in actions:
        if action not in ACTION_TO_CATEGORY:
            ACTION_TO_CATEGORY[action] = category


def _infer_category(action_type: str, behavior_mode: str = "Common") -> str:
    """
    action_type에서 action_category를 자동 추론.

    추론 로직:
    1. behavior_mode와 action_type이 모두 매칭되면 → behavior_mode 우선 사용
    2. 매칭 안 되면 → ACTION_TO_CATEGORY 역참조 테이블에서 찾기
    3. 그래도 없으면 → "Common" 폴백

    왜 behavior_mode를 우선하는가:
    "Follow"는 Common과 Social 양쪽에 존재. behavior_mode가 "Social"이면
    Social.Follow로 분류해야 BT 서브트리가 올바르게 동작함.
    """
    # behavior_mode에 해당하는 카테고리에 action_type이 있으면 그걸 사용
    mode_actions = CATEGORY_ACTION_MAP.get(behavior_mode, set())
    if action_type in mode_actions:
        return behavior_mode

    # 역참조 테이블에서 찾기
    return ACTION_TO_CATEGORY.get(action_type, "Common")


# ─────────────────────────────────────────────────────────────────────────────
# Gemini CLI용 구조화 프롬프트
# ActionType은 C++ Enum과 1:1 대응되므로 정확한 값만 사용해야 함.
# ─────────────────────────────────────────────────────────────────────────────
STRUCTURING_PROMPT = """You are an Action Structurer for a VR game engine.
Convert the NPC's natural language response into structured game actions.

NPC Response: "{raw_response}"
Behavior Mode: "{behavior_mode}"
Facial State: "{facial_state}"

Available Action Types (MUST use these exact strings):

[Common Actions] - Basic actions available in all modes
  Idle, Move, Follow, Wait, Dialogue, TurnTo, Stop, Scan, UseItem, Equip, Unequip

[Combat Actions] - Fighting and defense
  Attack, Block, Dodge, Flee, SignalAllies

[Social Actions] - Social interaction
  Trade, Emote, GiveItem, Comfort, HandObject

[Task Actions] - Object interaction
  PickUp, Drop, Craft, Repair

[Investigation Actions] - Searching and tracking
  Investigate, Track, Scout

[Lifestyle Actions] - Daily life activities
  Sit, Sleep, Clean, Read, Pray, Dance, Sing

MAPPING RULES:
1. Speech in "quotes" → ActionType: "Dialogue", Parameters: {{"text": "...", "tone": "..."}}
2. *runs/walks/goes to* → ActionType: "Move", Parameters: {{"target_loc": {{"x": 0, "y": 0, "z": 0}}, "style": "Run"/"Walk"}}
3. *attacks/strikes/hits* → ActionType: "Attack", Parameters: {{"target_id": "..."}}
4. *blocks/defends/shields* → ActionType: "Block"
5. *dodges/rolls/evades* → ActionType: "Dodge"
6. *opens/closes/takes/uses* → match to PickUp/Drop/UseItem as appropriate
7. *waves/nods/bows/smiles* → ActionType: "Emote", Parameters: {{"gesture": "Wave"/"Nod"/"Bow"}}
8. *sits/sits down* → ActionType: "Sit"
9. *waits/stops/pauses* → ActionType: "Wait", Parameters: {{"duration": "3.0"}}
10. *looks around/searches* → ActionType: "Scan" or "Investigate"

RULES:
- Always include a Dialogue action if there is speech in "quotes"
- Output ONLY a JSON array of action objects
- Each object MUST follow exactly this format: {{"ActionType": "...", "FacialState": "{facial_state}", "Parameters": {{...}}}}
- target_id and target_loc MUST be placed inside the Parameters dictionary.

Output format: [{{"ActionType": "...", "FacialState": "...", "Parameters": {{}}}}, ...]"""


# ─────────────────────────────────────────────────────────────────────────────
# 유효성 검증용 상수
# ─────────────────────────────────────────────────────────────────────────────
VALID_MOVE_STYLES = {"Walk", "Run", "Sprint", "Crouch", "Crawl"}


def _parse_mode_and_facial(raw_response: str) -> tuple[str, str, str]:
    """
    raw_response에서 [Mode: X] [Facial: Y] 태그를 파싱하고 제거.

    Returns:
        (behavior_mode, facial_state, cleaned_response)
    """
    mode = "Common"
    facial = "Neutral"

    mode_match = re.search(r'\[Mode:\s*(\w+)\]', raw_response, re.IGNORECASE)
    facial_match = re.search(r'\[Facial:\s*(\w+)\]', raw_response, re.IGNORECASE)

    if mode_match:
        mode = mode_match.group(1)
    if facial_match:
        facial = facial_match.group(1)

    # 태그 라인 제거
    cleaned = re.sub(
        r'\[Mode:\s*\w+\]\s*\[Facial:\s*\w+\]\s*\n?', '',
        raw_response, flags=re.IGNORECASE
    ).strip()

    return mode, facial, cleaned


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
    behavior_mode, facial_state, clean_response = _parse_mode_and_facial(raw_response)
    print(f"[Interface Output] Parsed Mode={behavior_mode}, Facial={facial_state}")
    print(f"[Interface Output] Structuring: '{clean_response[:80]}...'")

    # 2단계: Gemini CLI로 구조화 시도
    prompt = STRUCTURING_PROMPT.format(
        raw_response=clean_response,
        behavior_mode=behavior_mode,
        facial_state=facial_state
    )

    print("[Interface Output] Calling Gemini CLI for action structuring...")
    cli_result = call_gemini_cli(prompt, extract_json=True)

    action_batch = None

    if cli_result:
        try:
            actions_data = json.loads(cli_result)
            if isinstance(actions_data, dict):
                actions_data = [actions_data]

            actions = _parse_actions(actions_data, npc_id, behavior_mode)

            action_batch = ActionBatch(
                AgentID=npc_id,
                Mode=behavior_mode,
                Actions=actions
            )
            print(f"[Interface Output] Success: {len(actions)} actions created")

        except json.JSONDecodeError as e:
            print(f"[Interface Output] JSON parse error: {e}")
        except Exception as e:
            print(f"[Interface Output] Error: {e}")

    # 3단계: 실패 시 Regex 폴백
    if not action_batch:
        print("[Interface Output] CLI failed, using regex fallback...")
        action_batch = _regex_fallback_parse(clean_response, npc_id, behavior_mode, facial_state)

    return {
        "action_batch": action_batch,
        "current_speaker": "Interface_Output",
        "next": "Rules"
    }


def _parse_actions(actions_data: list, npc_id: str, behavior_mode: str) -> list:
    """
    Gemini CLI가 반환한 액션 딕셔너리 목록 → GameAction Pydantic 모델로 변환.
    """
    from ..schemas.actions import GameAction
    actions = []

    for action_dict in actions_data:
        # LLM이 Pydantic 필드명(PascalCase)을 따랐는지 지원
        action_type = action_dict.get("ActionType") or action_dict.get("action_type", "")
        
        # action_type 유효성 검증
        if action_type not in ACTION_TO_CATEGORY and action_type != "Dialogue":
            print(f"[Interface Output] Skipping invalid action_type: {action_type}")
            continue

        facial = action_dict.get("FacialState") or action_dict.get("emotion") or action_dict.get("facial_state", "Neutral")
        params = action_dict.get("Parameters") or action_dict.get("parameters", {})
        
        # 레거시(잘못 생성된) target_id, target_loc을 Parameters로 강제 편입
        if "target_id" in action_dict and "target_id" not in params:
            params["target_id"] = action_dict.get("target_id")
        if "target_loc" in action_dict and "target_loc" not in params:
            # GameVector3 변환은 내부적으로 처리 않음(JSON 직렬화만 수행)
            # dict 형식일 경우에 대비
            loc = action_dict.get("target_loc")
            if isinstance(loc, dict):
                # JSON 문자열 포맷팅
                params["target_loc"] = f'{{"x":{loc.get("x",0)},"y":{loc.get("y",0)},"z":{loc.get("z",0)}}}'
        
        # Move 스타일 보정
        if action_type == "Move" and params.get("style") not in VALID_MOVE_STYLES:
            params["style"] = "Walk"

        # 모든 Parameter를 string value로 변환 (C++ TMap<FString, FString> 대응)
        str_params = {}
        for k, v in params.items():
            if isinstance(v, (dict, list)):
                str_params[k] = json.dumps(v)
            else:
                str_params[k] = str(v)

        actions.append(GameAction(
            ActionType=action_type,
            FacialState=facial,
            Parameters=str_params
        ))

    return actions


def _regex_fallback_parse(raw_response: str, npc_id: str,
                          behavior_mode: str = "Common",
                          facial_state: str = "Neutral") -> ActionBatch:
    """
    Regex 기반 폴백 파서.
    CLI 실패 시 raw_response에서 "quotes", *asterisks*, (emotions)를 직접 추출.
    """
    from ..schemas.actions import GameAction
    actions = []

    # 1. 대사 추출 ("quotes" → Dialogue 액션)
    speech_matches = re.findall(r'"([^"]+)"', raw_response)
    if speech_matches:
        emotion_matches = re.findall(r'\(([^)]+)\)', raw_response)
        emotion = _normalize_emotion(emotion_matches[0]) if emotion_matches else "Neutral"

        actions.append(GameAction(
            ActionType="Dialogue",
            FacialState=emotion,
            Parameters={"text": speech_matches[0], "emotion": emotion},
        ))

    # 2. 물리 액션 추출 (*asterisks* → 해당 ActionType 매핑)
    action_matches = re.findall(r'\*([^*]+)\*', raw_response)
    for action_text in action_matches:
        parsed = _parse_natural_action(action_text, behavior_mode)
        if parsed:
            actions.append(parsed)

    # 3. 아무 액션도 없으면 전체를 Dialogue로 처리
    if not actions:
        clean_text = re.sub(r'[*()]', '', raw_response).strip()
        if clean_text:
            actions.append(GameAction(
                ActionType="Dialogue",
                FacialState="Neutral",
                Parameters={"text": clean_text[:200], "emotion": "Neutral"},
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
