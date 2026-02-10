"""
File: interface.py
Purpose: Interface Agent (Mediator).
Translates GesPrompt (Voice + Gesture) into structured Intent using the System Prompt.
"""
import json
from typing import Dict, Any, Optional
from .state import AgentState
from ..schemas.vr_context import GesPrompt
from ..schemas.intent import Intent
from ..utils.llm_factory import get_llm, call_gemini_cli
from langchain_core.prompts import ChatPromptTemplate

# SYSTEM PROMPT (Enhanced with target_npc extraction)
INTERFACE_SYSTEM_PROMPT = """Role: Interface Agent (Action Specialist)

You are an Action Analyst. Rapidly translate player input into structured Action Intents.
NOTE: Pure dialogue has already been filtered out. Your job is to analyze PHYSICAL ACTIONS.

Inputs:
- Spoken text (Command)
- Gesture metadata (Pointing, Grabbing)
- Contextual references

Responsibilities:
- Extract specific physical actions: "Move", "Attack", "Interact", "Emote", "Wait".
- Resolve targets: Who or What is the target? (e.g., "Door", "Goblin", "Elara").
- Fuse speech + gesture: "Attack that" + Pointing -> Attack TargetID.

TARGET_NPC EXTRACTION:
- If action targets an NPC, extract the name (e.g., "Elara", "James").

VALID action_type VALUES (Strict):
- "Move" : Movement (이동해, 와, 가, 앞으로, 따라와)
- "Attack" : Combat (공격해, 때려, 싸워, 죽여)
- "Interact" : Objects (열어, 집어, 켜, 꺼, 줘)
- "Emote" : Expressive (웃어, 울어, 춤춰)
- "Wait" : Stop/Halt (멈춰, 기다려)
- "Unknown" : If truly ambiguous (e.g., "어...")

CRITICAL EXAMPLES:
- "이리와" → action_type = "Move", target_reference="Player_1"
- "저 문 열어" → action_type = "Interact", target_reference="Door"
- "엘라라 공격해" → action_type = "Attack", target_npc="Elara"
- "따라와" → action_type = "Move"
- "앞으로 가" → action_type = "Move", target_location={front_vector}

Constraints:
- Do NOT output "Talk" or "Speak".
- Do not produce ActionBatch, only Intent JSON.
- If unsure, output "Unknown".

Output:
Structured intent objects only."""

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

    # --- 1. Fast Classification via RoBERTa ---
    from ..utils.intent_classifier import classify_intent
    
    print(f"[Interface] Classifying with RoBERT: '{transcript}'")
    intent_type = classify_intent(transcript)
    print(f"[Interface] Classifier result: {intent_type}")
    
    # Path A: Fast Track for Dialogue
    if intent_type == "speak":
        target_npc = extract_target_npc(transcript)
        print(f"[Interface] Fast Track result: Talk -> {target_npc}")
        intent = Intent(
            action_type="Talk",
            target_npc=target_npc,
            raw_query=transcript,
            confidence=0.95
        )
        return {
            "analysis": {"intent": intent},
            "current_speaker": "Interface",
            "next": "Supervisor"
        }

    # Path B: Deep Analysis via Gemini CLI (for Actions)
    # If RoBERTa says "else" (Move, Attack, Interact...), we need specific details.
    
    # Construct Prompt for CLI
    cli_system_prompt = INTERFACE_SYSTEM_PROMPT + "\n\nConvert this 'GesPrompt' context into a JSON Intent."
    
    # Serialize Context
    context_json = vr_context.model_dump_json()
    full_prompt = f"{cli_system_prompt}\n\n[Input Context]:\n{context_json}"
    
    print(f"[Interface] Calling Gemini CLI for Action Analysis...")
    
    intent = None
    
    # Call CLI using centralized function
    cli_result = call_gemini_cli(full_prompt, extract_json=True)
    
    if cli_result:
        try:
            # Parse
            data = json.loads(cli_result)
            intent = Intent(**data)
            print(f"[Interface] CLI Success: {intent.action_type} -> {intent.target_npc}")
        except Exception as e:
            print(f"[Interface] CLI Parse Error: {e}")
    else:
        print(f"[Interface] CLI Failed")

    # Fallback if CLI fails but intent was 'action'
    if not intent:
        print("[Interface] CLI Failed, using Fallback Action.")
        intent = Intent(
            action_type="Unknown", # Let Dialogue agent handle fallback or ask clarification
            raw_query=transcript,
            confidence=0.3
        )

    return {
        "analysis": {"intent": intent},
        "current_speaker": "Interface",
        "next": "Supervisor"
    }


def extract_target_npc(transcript: str) -> Optional[str]:
    """
    Extract NPC name from transcript by checking for file existence in the personas directory.
    Only recognizes exact English names (case-insensitive) that have a .yaml persona file.
    """
    from pathlib import Path
    
    # Path to the personas directory
    personas_dir = Path(__file__).parent / "personas"
    
    # Get all .yaml filenames (lowercase) as valid NPC names
    # This scans subdirectories like 'core' and 'generic'
    valid_names = [f.stem for f in personas_dir.glob("**/*.yaml")]
    
    # Check for name existence in transcript
    for name in valid_names:
        if name.lower() in transcript.lower():
            # Return the original case if possible, or just the filename stem
            return name.capitalize() if name.lower() == "elara" else name # Basic normalization
            
    return None

