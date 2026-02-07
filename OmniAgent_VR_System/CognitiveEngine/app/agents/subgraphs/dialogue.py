"""
File: dialogue.py
Purpose: Character Agent (Persona & Dialogue).
1. Loads Persona YAML dynamically based on AgentID.
2. Generates in-character responses using System Prompt.
3. Outputs SpeakAction only.
"""
import yaml
import os
from ...utils.llm_factory import get_llm
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState
from ...schemas.actions import ActionBatch, SpeakAction

PERSONAS_BASE_PATH = "app/agents/personas"

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

def load_persona(agent_id: str):
    """
    Load persona by agent_id.
    Searches in core/ first, then generic/.
    Falls back to default if not found.
    """
    # Normalize agent_id to lowercase for filename matching
    agent_lower = agent_id.lower()
    
    # Search paths in order
    search_paths = [
        os.path.join(PERSONAS_BASE_PATH, "core", f"{agent_lower}.yaml"),
        os.path.join(PERSONAS_BASE_PATH, "generic", f"{agent_lower}.yaml"),
    ]
    
    for path in search_paths:
        if os.path.exists(path):
            print(f"[Dialogue] Loading persona from: {path}")
            with open(path, 'r', encoding='utf-8') as f:
                return yaml.safe_load(f)
    
    # Fallback to default persona
    default_path = os.path.join(PERSONAS_BASE_PATH, "core", "elara.yaml")
    print(f"[Dialogue] Persona '{agent_id}' not found, using default: {default_path}")
    if os.path.exists(default_path):
        with open(default_path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)
    
    return None

def dialogue_node(state: AgentState):
    """
    Dialogue Agent (Character).
    Generates SpeakAction based on Persona and Memory using LLM.
    """
    # Get agent_id from vr_context (who the player is talking to)
    vr_context = state.get("vr_context")
    agent_id = "Elara"  # Default
    
    if vr_context:
        if hasattr(vr_context, 'looking_at_entity_id') and vr_context.looking_at_entity_id:
            agent_id = vr_context.looking_at_entity_id
        elif isinstance(vr_context, dict) and vr_context.get("looking_at_entity_id"):
            agent_id = vr_context.get("looking_at_entity_id")
    
    print(f"[Dialogue] Agent ID: {agent_id}")
    
    # Load persona dynamically
    persona = load_persona(agent_id)
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
