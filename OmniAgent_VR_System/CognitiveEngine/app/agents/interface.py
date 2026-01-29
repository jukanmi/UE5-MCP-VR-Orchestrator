"""
File: interface.py
Purpose: Interface Agent (Mediator).
Translates GesPrompt (Voice + Gesture) into structured Intent using the System Prompt.
"""
from typing import Dict, Any
from .state import AgentState
from ..schemas.vr_context import GesPrompt
from ..schemas.intent import Intent
from ..utils.llm_factory import get_llm
from langchain_core.prompts import ChatPromptTemplate

# SYSTEM PROMPT (Verbatim from systemPrompt.txt)
INTERFACE_SYSTEM_PROMPT = """Role: Interface Agent (Mediator)

You translate player input into structured intent.

Inputs may include:

Spoken text
Gesture metadata (e.g. pointing_at_actor_id)
Contextual references from the engine

Responsibilities:

Resolve deictic expressions such as “this”, “that”, “over there”.
Fuse speech and gesture into explicit references.
Produce clean, ambiguity-free intent representations.

Constraints:

Do not generate actions directly.
Do not apply rules or validation.
Do not guess missing references; ask for clarification if unresolved.

Output:

Structured intent objects only.
No ActionBatch, no engine commands."""

def interface_node(state: AgentState) -> dict:
    """
    Interface Agent Node.
    Analyzes GesPrompt and produces an Intent using LLM.
    """
    vr_context: GesPrompt = state.get("vr_context")
    if not vr_context:
        return {"next": "End"}

    transcript = vr_context.voice_transcript
    
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

    # --- Fast Reflex (Hardcoded Logic for Latency Masking) ---
    def check_fast_reflex(text: str) -> Optional[Intent]:
        text_lower = text.lower()
        # Safety/Stop
        if any(w in text_lower for w in ["멈춰", "그만", "stop", "halt"]):
            return Intent(action_type="Wait", raw_query=text, confidence=1.0)
        # Simple Greeting
        if any(w in text_lower for w in ["안녕", "hello", "hi"]):
            return Intent(action_type="Talk", raw_query=text, confidence=1.0)
        return None

    reflex = check_fast_reflex(transcript)
    if reflex:
        print(f"Reflex Triggered: {reflex.action_type}")
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
        
        prompt = ChatPromptTemplate.from_messages([
            ("system", INTERFACE_SYSTEM_PROMPT),
            ("human", """
            User Transcript: "{transcript}"
            Gesture Data:
            {gestures}
            
            Based on the above, extract the user's Intent.
            """)
        ])
        
        chain = prompt | structured_llm
        intent = chain.invoke({"transcript": transcript, "gestures": gesture_str})
        
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
