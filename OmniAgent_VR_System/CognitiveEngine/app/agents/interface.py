"""
File: interface.py
Purpose: Interface Agent (Mediator).
Translates GesPrompt (Voice + Gesture) into structured Intent using the System Prompt.
"""
from typing import Dict, Any, Optional
from .state import AgentState
from ..schemas.vr_context import GesPrompt
from ..schemas.intent import Intent
from ..utils.llm_factory import get_llm
from langchain_core.prompts import ChatPromptTemplate

# SYSTEM PROMPT (Enhanced with action_type list)
INTERFACE_SYSTEM_PROMPT = """Role: Interface Agent (Mediator)

You translate player input into structured intent.

Inputs may include:
- Spoken text
- Gesture metadata (e.g. pointing_at_actor_id)
- Contextual references from the engine

Responsibilities:
- Resolve deictic expressions such as "this", "that", "over there".
- Fuse speech and gesture into explicit references.
- Produce clean, ambiguity-free intent representations.

VALID action_type VALUES (MUST use one of these):
- "Move" : Movement commands (이동해, 와, 따라와, 가, 앞으로, 뒤로, 접근해, 여기로)
- "Attack" : Combat actions (공격해, 때려, 싸워)
- "Interact" : Object interactions (열어, 집어, 사용해, 줘)
- "Talk" : Conversation/dialogue requests (말해, 대화해, 이야기해)
- "Wait" : Stop/pause commands (멈춰, 기다려, 그만)
- "Emote" : Emotional expressions (웃어, 울어, 인사해)
- "Unknown" : Cannot determine intent

CRITICAL:
- "이동해", "와", "내 앞으로 와", "여기로 와", "따라와" → action_type = "Move"
- Movement requests MUST use action_type "Move", NOT "Talk"

Constraints:
- Do not generate actions directly.
- Do not apply rules or validation.
- Do not guess missing references; ask for clarification if unresolved.

Output:
Structured intent objects only.
No ActionBatch, no engine commands."""

def interface_node(state: AgentState) -> dict:
    """
    Interface Agent Node.
    Analyzes GesPrompt and produces an Intent using LLM.
    """
    vr_context = state.get("vr_context")
    
    # Debug logging
    print(f"[Interface] Raw state keys: {state.keys()}")
    print(f"[Interface] vr_context type: {type(vr_context)}")
    print(f"[Interface] vr_context value: {vr_context}")
    
    if not vr_context:
        print("[Interface] ERROR: vr_context is None or empty!")
        return {"next": "End"}

    # Handle both dict and GesPrompt object
    if isinstance(vr_context, dict):
        print("[Interface] vr_context is dict, converting to GesPrompt...")
        try:
            vr_context = GesPrompt(**vr_context)
        except Exception as e:
            print(f"[Interface] Failed to convert dict to GesPrompt: {e}")
            return {"next": "End"}

    transcript = vr_context.voice_transcript
    print(f"[Interface] Transcript: {transcript}")
    
    # Extract player location if available
    player_loc = None
    if vr_context.player_location:
        player_loc = {
            "x": vr_context.player_location.x,
            "y": vr_context.player_location.y,
            "z": vr_context.player_location.z
        }
    
    # Format Gesture Data for Prompt
    gestures_desc = []
    for g in vr_context.gestures:
        desc = f"- Type: {g.gesture_type}, Target: {g.target_entity_id}, Hand: {g.hand}"
        if g.location:
            desc += f", Location: {g.location}"
        if g.held_object_id:
            desc += f", Holding: {g.held_object_id}"
        gestures_desc.append(desc)
    gesture_str = "\n".join(gestures_desc) if gestures_desc else "None"

    # --- Emergency Interrupt (High Priority) ---
    if vr_context.last_event in ["Hit", "Ambush"]:
        print(f"!!! EMERGENCY INTERRUPT: {vr_context.last_event} !!!")
        return {
            "analysis": {"intent": Intent(
                action_type="Attack",
                target_reference="Attacker",
                raw_query=f"[System Event: {vr_context.last_event}]",
                confidence=1.0
            )},
            "current_speaker": "Interface",
            "next": "Supervisor"
        }
    # -------------------------------------------

    # --- Fast Reflex (Hardcoded Logic for Latency Masking) ---
    def check_fast_reflex(text: str) -> Optional[Intent]:
        text_lower = text.lower()
        # Safety/Stop
        if any(w in text_lower for w in ["멈춰", "그만", "stop", "halt"]):
            return Intent(action_type="Wait", raw_query=text, confidence=1.0)
        # Simple Greeting
        if any(w in text_lower for w in ["안녕", "hello", "hi"]):
            return Intent(action_type="Talk", raw_query=text, confidence=1.0)
        # Movement commands - include player location!
        if any(w in text_lower for w in ["이동해", "여기로 와", "내 앞으로", "따라와", "이리 와", "이리와", "와라", "와"]):
            return Intent(
                action_type="Move", 
                target_reference="Player_1",
                target_location=player_loc,  # Include player location!
                raw_query=text, 
                confidence=1.0
            )
        return None

    reflex = check_fast_reflex(transcript)
    if reflex:
        print(f"Reflex Triggered: {reflex.action_type}, target_location: {reflex.target_location}")
        return {
            "analysis": {"intent": reflex},
            "current_speaker": "Interface",
            "next": "Supervisor"
        }
    # ---------------------------------------------------------

    # LLM Setup
    try:
        llm = get_llm(temperature=0.0)
        structured_llm = llm.with_structured_output(Intent)
        
        # Format Stats
        stats_str = "None"
        if vr_context.stats:
            stats_str = ", ".join([f"{k}: {v}" for k, v in vr_context.stats.items()])

        # Include player location in context
        player_loc_str = "None"
        if player_loc:
            player_loc_str = f"x={player_loc['x']}, y={player_loc['y']}, z={player_loc['z']}"

        prompt = ChatPromptTemplate.from_messages([
            ("system", INTERFACE_SYSTEM_PROMPT),
            ("human", """
            User Transcript: "{transcript}"
            Gesture Data:
            {gestures}
            NPC Stats: {stats}
            Player Location: {player_location}
            
            Based on the above, extract the user's Intent.
            If the command is about movement toward the player (e.g. "come here", "이리와"), 
            set target_reference to "Player_1" and use the player_location for target_location.
            """)
        ])
        
        chain = prompt | structured_llm
        intent = chain.invoke({
            "transcript": transcript, 
            "gestures": gesture_str, 
            "stats": stats_str,
            "player_location": player_loc_str
        })
        
        # If Move intent but no location, try to add player location
        if intent.action_type == "Move" and not intent.target_location and player_loc:
            intent.target_location = player_loc
            intent.target_reference = intent.target_reference or "Player_1"
        
        # Add debug info for raw query if missing
        if not intent.raw_query:
            intent.raw_query = transcript

    except Exception as e:
        print(f"LLM Error in Interface: {e}. Falling back to Unknown.")
        intent = Intent(
            action_type="Unknown",
            raw_query=transcript,
            confidence=0.0
        )
    
    return {
        "analysis": {"intent": intent},
        "current_speaker": "Interface",
        "next": "Supervisor"
    }
