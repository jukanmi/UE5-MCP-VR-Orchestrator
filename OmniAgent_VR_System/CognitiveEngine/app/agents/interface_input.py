"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: interface_input.py                                                    ║
║ Role: INPUT ADAPTER (UE5 → LLM)                                             ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Transform structured game engine data (GesPrompt) into natural language   ║
║   that LLMs can understand and reason about naturally.                      ║
║                                                                              ║
║ INPUT:  GesPrompt (JSON) - voice_transcript, gestures, location, stats      ║
║ OUTPUT: natural_context (str) - Human-readable summary                      ║
║                                                                              ║
║ CONSTRAINTS:                                                                 ║
║   - Must preserve semantic meaning from UE5 data                            ║
║   - Must be concise (under 3 sentences)                                     ║
║   - Must handle emergency events (Hit/Ambush) with bypass logic             ║
║   - Must use Gemini CLI (free) for cost optimization                        ║
║                                                                              ║
║ EXAMPLE TRANSFORMATION:                                                     ║
║   IN:  {voice_transcript: "이리와", gestures: [{type: "Point"}]}            ║
║   OUT: "Player at (100,50,20) is pointing and calling someone to come"      ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import json
from .state import AgentState
from ..schemas.vr_context import GesPrompt
from ..utils.llm_factory import call_gemini_cli


def format_gestures_for_prompt(gestures) -> str:
    """Format gesture data into human-readable text."""
    if not gestures:
        return "None"
    
    descriptions = []
    for g in gestures:
        desc = f"- {g.gesture_type}"
        if g.target_entity_id:
            desc += f" at {g.target_entity_id}"
        if g.hand:
            desc += f" ({g.hand} hand)"
        if g.location:
            desc += f" at location {g.location}"
        if g.held_object_id:
            desc += f", holding {g.held_object_id}"
        descriptions.append(desc)
    
    return "\n".join(descriptions)


def format_stats_for_prompt(stats) -> str:
    """Format player stats into readable text."""
    if not stats:
        return "Unknown"
    return ", ".join([f"{k}: {v}" for k, v in stats.items()])


CONTEXT_CONVERSION_PROMPT = """You are a Context Translator for a VR game AI system.
Convert the following structured game data into a concise natural language summary.

Rules:
- Be clear and specific about what the player wants
- Include spatial information if available
- Include emotional/urgency context from events
- Keep it under 3 sentences
- Write in English

Input Data:
- Player said: "{transcript}"
- Player gestures: {gestures}
- Player location: {location}
- Looking at: {looking_at}
- Last event: {last_event}
- Player stats: {stats}

Output a single natural language summary paragraph. No JSON, no formatting."""


def interface_input_node(state: AgentState):
    """
    Interface Input Agent (LLM #1).
    
    Converts structured UE5 GesPrompt into natural language context
    using Gemini CLI (free).
    
    Input: AgentState with vr_context (GesPrompt)
    Output: AgentState with natural_context (str)
    """
    vr_context = state.get("vr_context")
    
    if not vr_context:
        print("[Interface Input] ERROR: No vr_context found")
        return {
            "natural_context": "Player input is empty.",
            "current_speaker": "Interface_Input",
            "next": "Dialogue"
        }
    
    # Handle both dict and GesPrompt object
    if isinstance(vr_context, dict):
        try:
            vr_context = GesPrompt(**vr_context)
        except Exception as e:
            print(f"[Interface Input] Failed to convert dict: {e}")
            return {
                "natural_context": f"Player said something but context is unclear.",
                "current_speaker": "Interface_Input",
                "next": "Dialogue"
            }
    
    transcript = vr_context.voice_transcript
    print(f"[Interface Input] Transcript: '{transcript}'")
    
    # --- Emergency Interrupt (bypass LLM for speed) ---
    if vr_context.last_event in ["Hit", "Ambush"]:
        print(f"[Interface Input] !!! EMERGENCY: {vr_context.last_event} !!!")
        emergency_context = (
            f"EMERGENCY: Player is under attack ({vr_context.last_event})! "
            f"Player said: \"{transcript}\". "
            f"Immediate combat response required."
        )
        return {
            "natural_context": emergency_context,
            "current_speaker": "Interface_Input",
            "next": "Dialogue"
        }
    
    # Format location
    location_str = "Unknown"
    if vr_context.player_location:
        loc = vr_context.player_location
        location_str = f"({loc.x}, {loc.y}, {loc.z})"
    
    # Format gestures
    gesture_str = format_gestures_for_prompt(vr_context.gestures)
    
    # Format stats
    stats_str = format_stats_for_prompt(vr_context.stats)
    
    # Build prompt
    prompt = CONTEXT_CONVERSION_PROMPT.format(
        transcript=transcript,
        gestures=gesture_str,
        location=location_str,
        looking_at=vr_context.looking_at_entity_id or "Nothing specific",
        last_event=vr_context.last_event or "None",
        stats=stats_str
    )
    
    print("[Interface Input] Calling Gemini CLI for context conversion...")
    natural_context = call_gemini_cli(prompt, extract_json=False)
    
    if not natural_context:
        # Fallback: construct basic context without LLM
        print("[Interface Input] CLI failed, using manual fallback")
        natural_context = f"Player said: \"{transcript}\""
        if vr_context.looking_at_entity_id:
            natural_context += f", looking at {vr_context.looking_at_entity_id}"
        if gesture_str != "None":
            natural_context += f", with gestures: {gesture_str}"
    
    print(f"[Interface Input] Natural context: {natural_context[:100]}...")
    
    # Extract target NPC (simple heuristic, preserved from original)
    target_npc = _extract_target_npc(transcript, vr_context)
    
    return {
        "natural_context": natural_context,
        "target_npc": target_npc,
        "current_speaker": "Interface_Input",
        "next": "Dialogue"
    }


def _extract_target_npc(transcript: str, vr_context: GesPrompt) -> str:
    """
    Extract target NPC from transcript or context.
    Simple heuristic - can be enhanced later.
    """
    # Known NPC names (can be loaded from config later)
    known_npcs = ["elara", "james", "guard", "merchant", "blacksmith"]
    
    transcript_lower = transcript.lower()
    for npc in known_npcs:
        if npc in transcript_lower:
            return npc.capitalize()
    
    # Fallback to looking_at entity
    if vr_context.looking_at_entity_id:
        return vr_context.looking_at_entity_id
    
    # Default
    return "Elara"
