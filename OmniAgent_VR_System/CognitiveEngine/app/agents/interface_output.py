"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: interface_output.py                                                   ║
║ Role: OUTPUT ADAPTER (LLM → UE5)                                            ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Transform free-form NPC response into structured ActionBatch that UE5     ║
║   can execute. Parses natural language into game engine function calls.     ║
║                                                                              ║
║ INPUT:  raw_response (str) - "Quote" + *action* + (emotion)                 ║
║ OUTPUT: ActionBatch - [SpeakAction, GameAction, ...]                        ║
║                                                                              ║
║ PARSING RULES:                                                               ║
║   - "Quotes"       → SpeakAction(text=...)                                  ║
║   - *asterisks*    → GameAction(action_type=Move/Attack/Interact/...)       ║
║   - (parentheses)  → emotion field                                          ║
║                                                                              ║
║ FALLBACK STRATEGY:                                                           ║
║   1. Try Gemini CLI for structured extraction (preferred)                   ║
║   2. If fails, use regex-based parser (robust backup)                       ║
║   3. If all fails, return minimal SpeakAction("...", Confused)              ║
║                                                                              ║
║ EXAMPLE TRANSFORMATION:                                                     ║
║   IN:  "Of course! (cheerfully) *runs to the door*"                         ║
║   OUT: [SpeakAction(text="Of course!", emotion="Happy"),                    ║
║         GameAction(type="Move", parameters={style:"Run"})]                  ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import json
import re
from .state import AgentState
from ..schemas.actions import ActionBatch, SpeakAction, GameAction
from ..utils.llm_factory import call_gemini_cli


# Valid action types (must match GameAction Literal)
VALID_ACTION_TYPES = {"Move", "Attack", "Interact", "Emote", "Speak", "Wait"}
VALID_MOVE_STYLES = {"Walk", "Run", "Crawl"}
VALID_INTERACT_TYPES = {"Open", "Close", "Take", "Use"}


STRUCTURING_PROMPT = """You are an Action Structurer for a VR game engine.
Convert the NPC's natural language response into structured game actions.

NPC Response: "{raw_response}"
NPC ID: "{npc_id}"

Available Action Types:
1. Speak: Extract text from "quotes", emotion from (parentheses)
   → {{"action_type": "Speak", "executor_npc_id": "{npc_id}", "text": "...", "emotion": "Happy"}}

2. Move: Extract from *runs/walks/crawls/goes* keywords
   → {{"action_type": "Move", "executor_npc_id": "{npc_id}", "target_id": "Player", "parameters": {{"style": "Run"}}}}
   styles: "Walk", "Run", "Crawl"

3. Attack: Extract from *attacks/strikes/hits/swings* keywords
   → {{"action_type": "Attack", "executor_npc_id": "{npc_id}", "target_id": "Enemy", "parameters": {{"damage": "50", "style": "Melee"}}}}

4. Interact: Extract from *opens/closes/takes/uses* keywords
   → {{"action_type": "Interact", "executor_npc_id": "{npc_id}", "target_id": "Door", "parameters": {{"type": "Open"}}}}

5. Emote: Extract from gestures/expressions
   → {{"action_type": "Emote", "executor_npc_id": "{npc_id}", "parameters": {{"name": "Wave"}}}}

6. Wait: Extract from *stops/waits/pauses*
   → {{"action_type": "Wait", "executor_npc_id": "{npc_id}", "parameters": {{"duration": "3.0"}}}}

RULES:
- Always include a Speak action if there is text in "quotes"
- Map movement keywords to appropriate style (Walk/Run/Crawl)
- Default target_id is "Player" if not specified
- Output ONLY a JSON array of actions, nothing else

Output format: [{{"action_type": "...", ...}}, ...]"""


def interface_output_node(state: AgentState):
    """
    Interface Output Agent (LLM #3).
    
    Converts Dialogue's free-form response into structured ActionBatch
    using Gemini CLI (free).
    
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
    
    print(f"[Interface Output] Structuring response: '{raw_response[:80]}...'")
    
    # Build prompt
    prompt = STRUCTURING_PROMPT.format(
        raw_response=raw_response,
        npc_id=npc_id
    )
    
    # Call Gemini CLI
    print("[Interface Output] Calling Gemini CLI for action structuring...")
    cli_result = call_gemini_cli(prompt, extract_json=True)
    
    action_batch = None
    
    if cli_result:
        try:
            actions_data = json.loads(cli_result)
            
            # Handle both single dict and list
            if isinstance(actions_data, dict):
                actions_data = [actions_data]
            
            actions = _parse_actions(actions_data, npc_id)
            
            action_batch = ActionBatch(
                agent_id=npc_id,
                actions=actions,
                reasoning=f"Structured from: {raw_response[:200]}"
            )
            print(f"[Interface Output] Success: {len(actions)} actions created")
            
        except json.JSONDecodeError as e:
            print(f"[Interface Output] JSON parse error: {e}")
        except Exception as e:
            print(f"[Interface Output] Error: {e}")
    
    # Fallback: regex-based parsing if CLI fails
    if not action_batch:
        print("[Interface Output] CLI failed, using regex fallback...")
        action_batch = _regex_fallback_parse(raw_response, npc_id)
    
    return {
        "action_batch": action_batch,
        "current_speaker": "Interface_Output",
        "next": "Rules"
    }


def _parse_actions(actions_data: list, npc_id: str) -> list:
    """Parse action dictionaries into proper Pydantic models."""
    actions = []
    
    for action_dict in actions_data:
        action_type = action_dict.get("action_type", "")
        
        # Ensure executor_npc_id is set
        action_dict["executor_npc_id"] = action_dict.get("executor_npc_id", npc_id)
        
        if action_type == "Speak":
            actions.append(SpeakAction(
                executor_npc_id=action_dict["executor_npc_id"],
                text=action_dict.get("text", "..."),
                emotion=action_dict.get("emotion", "Neutral")
            ))
        elif action_type in VALID_ACTION_TYPES:
            # Validate and fix parameters
            params = action_dict.get("parameters", {})
            
            if action_type == "Move":
                style = params.get("style", "Walk")
                if style not in VALID_MOVE_STYLES:
                    params["style"] = "Walk"
            
            if action_type == "Interact":
                itype = params.get("type", "Use")
                if itype not in VALID_INTERACT_TYPES:
                    params["type"] = "Use"
            
            actions.append(GameAction(
                action_type=action_type,
                executor_npc_id=action_dict["executor_npc_id"],
                target_id=action_dict.get("target_id"),
                parameters=params
            ))
        else:
            print(f"[Interface Output] Skipping invalid action type: {action_type}")
    
    return actions


def _regex_fallback_parse(raw_response: str, npc_id: str) -> ActionBatch:
    """
    Fallback parser using regex when CLI fails.
    Extracts speech, actions, and emotions from the formatted response.
    """
    actions = []
    
    # 1. Extract speech (text in quotes)
    speech_matches = re.findall(r'"([^"]+)"', raw_response)
    if speech_matches:
        # Get emotion if available
        emotion_matches = re.findall(r'\(([^)]+)\)', raw_response)
        emotion = emotion_matches[0] if emotion_matches else "Neutral"
        
        # Normalize emotion
        emotion = _normalize_emotion(emotion)
        
        actions.append(SpeakAction(
            executor_npc_id=npc_id,
            text=speech_matches[0],
            emotion=emotion
        ))
    
    # 2. Extract physical actions (text in *asterisks*)
    action_matches = re.findall(r'\*([^*]+)\*', raw_response)
    for action_text in action_matches:
        parsed = _parse_natural_action(action_text, npc_id)
        if parsed:
            actions.append(parsed)
    
    # If no actions at all, create a default speak
    if not actions:
        # Try to use the whole response as speech
        clean_text = re.sub(r'[*()]', '', raw_response).strip()
        if clean_text:
            actions.append(SpeakAction(
                executor_npc_id=npc_id,
                text=clean_text[:200],  # Limit length
                emotion="Neutral"
            ))
    
    return ActionBatch(
        agent_id=npc_id,
        actions=actions,
        reasoning=f"Regex fallback from: {raw_response[:200]}"
    )


def _parse_natural_action(text: str, npc_id: str):
    """Parse a single natural language action description into GameAction."""
    text_lower = text.lower()
    
    # Move patterns (Korean + English)
    if any(word in text_lower for word in ["뛰어", "달려", "run", "rush", "sprint", "빠르게"]):
        return GameAction(
            action_type="Move",
            executor_npc_id=npc_id,
            target_id="Player",
            parameters={"style": "Run"}
        )
    elif any(word in text_lower for word in ["걸어", "천천히", "walk", "slowly", "다가"]):
        return GameAction(
            action_type="Move",
            executor_npc_id=npc_id,
            target_id="Player",
            parameters={"style": "Walk"}
        )
    elif any(word in text_lower for word in ["기어", "crawl", "sneak"]):
        return GameAction(
            action_type="Move",
            executor_npc_id=npc_id,
            target_id="Player",
            parameters={"style": "Crawl"}
        )
    
    # Attack patterns
    elif any(word in text_lower for word in ["공격", "attack", "strike", "hit", "swing", "베", "때"]):
        return GameAction(
            action_type="Attack",
            executor_npc_id=npc_id,
            target_id="Enemy",
            parameters={"damage": "50", "style": "Melee"}
        )
    
    # Interact patterns
    elif any(word in text_lower for word in ["열", "open", "opens"]):
        return GameAction(
            action_type="Interact",
            executor_npc_id=npc_id,
            target_id="Door",
            parameters={"type": "Open"}
        )
    elif any(word in text_lower for word in ["닫", "close", "closes"]):
        return GameAction(
            action_type="Interact",
            executor_npc_id=npc_id,
            target_id="Door",
            parameters={"type": "Close"}
        )
    elif any(word in text_lower for word in ["집", "take", "grab", "pick"]):
        return GameAction(
            action_type="Interact",
            executor_npc_id=npc_id,
            parameters={"type": "Take"}
        )
    
    # Emote patterns
    elif any(word in text_lower for word in ["웃", "smile", "laugh", "nod", "bow", "wave"]):
        name = "Smile"
        if "bow" in text_lower or "인사" in text_lower:
            name = "Bow"
        elif "wave" in text_lower or "손" in text_lower:
            name = "Wave"
        elif "nod" in text_lower or "끄덕" in text_lower:
            name = "Nod"
        return GameAction(
            action_type="Emote",
            executor_npc_id=npc_id,
            parameters={"name": name}
        )
    
    # Wait patterns
    elif any(word in text_lower for word in ["멈", "기다", "wait", "stop", "pause"]):
        return GameAction(
            action_type="Wait",
            executor_npc_id=npc_id,
            parameters={"duration": "3.0"}
        )
    
    return None


def _normalize_emotion(emotion_text: str) -> str:
    """Normalize emotion text to standard values."""
    emotion_lower = emotion_text.lower()
    
    emotion_map = {
        "기쁘": "Happy", "행복": "Happy", "happy": "Happy",
        "cheerful": "Happy", "joy": "Happy",
        "슬프": "Sad", "우울": "Sad", "sad": "Sad",
        "화나": "Angry", "분노": "Angry", "angry": "Angry",
        "무서": "Scared", "fear": "Scared", "scared": "Scared",
        "놀라": "Surprised", "surprised": "Surprised",
        "결연": "Determined", "determined": "Determined",
        "걱정": "Worried", "worried": "Worried",
    }
    
    for keyword, emotion in emotion_map.items():
        if keyword in emotion_lower:
            return emotion
    
    return "Neutral"


def _create_empty_batch(npc_id: str) -> ActionBatch:
    """Create an empty ActionBatch as fallback."""
    return ActionBatch(
        agent_id=npc_id,
        actions=[SpeakAction(
            executor_npc_id=npc_id,
            text="...",
            emotion="Confused"
        )],
        reasoning="Fallback: No raw_response available"
    )
