"""
File: dialogue.py
Purpose: Character Agent (Persona & Dialogue).
1. Loads Persona YAML (with memory_summary).
2. Generates in-character responses using System Prompt.
3. Outputs SpeakAction only.
"""
import yaml
import os
from ...utils.llm_factory import get_llm
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState
from ...schemas.actions import ActionBatch, SpeakAction

PERSONA_PATH = "app/agents/personas/core/elara.yaml"

# SYSTEM PROMPT (Verbatim)
DIALOGUE_SYSTEM_PROMPT = """Role: Dialogue Agent

You are responsible for generating communicative intent only.

Responsibilities:

Decide what the character wants to say.
Attach emotional context to speech.
Specify the intended listener if applicable.

Constraints:

Only produce SpeakAction proposals.
Do not reference game rules, limits, or engine behavior.
Do not output ActionBatch directly.

Output Format:

One or more SpeakAction proposals in structured JSON.
Ensure "action_type" is strictly "Speak".
Do not include narration or explanations."""

def load_persona(path: str):
    if not os.path.exists(path):
        return None
    with open(path, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)

def dialogue_node(state: AgentState):
    """
    Dialogue Agent (Character).
    Generates SpeakAction based on Persona and Memory using LLM.
    """
    persona = load_persona(PERSONA_PATH)
    if not persona:
        return {"next": "Error", "messages": ["System: Persona not found"]}
        
    memory = persona.get("memory_summary", {})
    key_events = memory.get("key_events", [])
    sentiment = memory.get("sentiment", "Neutral")
    
    # Check if we are in Fallback mode
    current_speaker = state.get("current_speaker")
    messages = state.get("messages", [])
    fallback_reason = ""
    if current_speaker == "Supervisor_Fallback" and messages:
        fallback_reason = f" (Context: {messages[-1]})"
    
    intent_data = state.get("analysis", {}).get("intent")
    user_input = intent_data.raw_query if intent_data else "..."
    # If fallback, we might not have raw query or it might be irrelevant, but good context.
    
    # LLM Construction
    llm = get_llm(temperature=0.7) # Higher temperature for creative dialogue

    system_message = f"""{DIALOGUE_SYSTEM_PROMPT}

    --- Persona Context ---
    Name: {persona['name']}
    Role: {persona['role']}
    Traits: {', '.join(persona['traits'])}
    Memory Summary: {'; '.join(key_events)}
    Sentiment toward Player: {sentiment}
    """
    
    human_message = f"Player said: '{user_input}'"
    if fallback_reason:
        human_message += f"\n[System Note]: The player's previous action was rejected. Reason: {fallback_reason}. Explain this in character."

    # We want structured output (SpeakAction)? 
    # The Dialogue System Prompt says "Output Format: One or more SpeakAction proposals in structured JSON."
    # So we use structured output again.
    structured_llm = llm.with_structured_output(SpeakAction)
    
    prompt = ChatPromptTemplate.from_messages([
        ("system", system_message),
        ("human", human_message)
    ])
    
    try:
        chain = prompt | structured_llm
        speak_action = chain.invoke({})
        
        # Wrap in Batch
        batch = ActionBatch(
            agent_id=persona['name'],
            actions=[speak_action]
        )
    except Exception as e:
        print(f"LLM Logic Error in Dialogue: {e}")
        # Fallback Mock
        batch = ActionBatch(
            agent_id=persona['name'],
            actions=[SpeakAction(text="...", emotion="Neutral")]
        )

    return {
        "action_batch": batch,
        "current_speaker": "Dialogue",
        "next": "End" 
    }
