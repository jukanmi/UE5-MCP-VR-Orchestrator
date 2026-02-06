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

# SYSTEM PROMPT (No curly braces that could be misinterpreted)
DIALOGUE_SYSTEM_PROMPT = """Role: Dialogue Agent

You are responsible for generating communicative intent only.

Responsibilities:
- Decide what the character wants to say.
- Attach emotional context to speech.
- Specify the intended listener if applicable.

Constraints:
- Only produce SpeakAction proposals.
- Do not reference game rules, limits, or engine behavior.
- Do not output ActionBatch directly.

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
    
    # LLM Construction
    llm = get_llm(temperature=0.7)

    # Build persona context as a single string (avoid template variable issues)
    persona_name = persona.get('name', 'Unknown')
    persona_role = persona.get('role', 'Unknown')
    persona_traits = ', '.join(persona.get('traits', []))
    memory_summary = '; '.join(key_events) if key_events else 'None'
    
    system_content = f"""{DIALOGUE_SYSTEM_PROMPT}

--- Persona Context ---
Name: {persona_name}
Role: {persona_role}
Traits: {persona_traits}
Memory Summary: {memory_summary}
Sentiment toward Player: {sentiment}
"""
    
    human_content = f"Player said: '{user_input}'"
    if fallback_reason:
        human_content += f"\n[System Note]: The player's previous action was rejected. Reason: {fallback_reason}. Explain this in character."

    structured_llm = llm.with_structured_output(SpeakAction)
    
    prompt = ChatPromptTemplate.from_messages([
        ("system", "{system_message}"),
        ("human", "{human_message}")
    ])
    
    try:
        chain = prompt | structured_llm
        speak_action = chain.invoke({
            "system_message": system_content,
            "human_message": human_content
        })
        
        # Wrap in Batch
        batch = ActionBatch(
            agent_id=persona_name,
            actions=[speak_action]
        )
    except Exception as e:
        print(f"LLM Logic Error in Dialogue: {e}")
        # Fallback Mock
        batch = ActionBatch(
            agent_id=persona_name,
            actions=[SpeakAction(text="...", emotion="Neutral")]
        )

    return {
        "action_batch": batch,
        "current_speaker": "Dialogue",
        "next": "End" 
    }
