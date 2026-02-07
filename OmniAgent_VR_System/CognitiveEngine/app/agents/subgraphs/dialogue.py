"""
File: dialogue.py
Purpose: Character Agent (Persona & Dialogue) with RAG.
1. Loads Persona YAML dynamically based on AgentID.
2. Retrieves relevant knowledge via RAG.
3. Generates in-character responses using System Prompt.
4. Outputs SpeakAction only.
"""
import yaml
import os
from ...utils.llm_factory import get_dialogue_llm
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState
from ...schemas.actions import ActionBatch, SpeakAction

PERSONAS_BASE_PATH = "app/agents/personas"

# SYSTEM PROMPT - Plain text dialogue (Gemma 3 doesn't support JSON mode)
DIALOGUE_SYSTEM_PROMPT = """You are a character in an immersive game world.

Respond ONLY with what the character would say - no narration, no action descriptions, no quotation marks.

Guidelines:
- Stay completely in character based on the personality and knowledge provided.
- Use the character's knowledge naturally, don't quote it verbatim.
- Keep responses concise and natural (1-3 sentences typically).
- Match the emotional tone to the situation.

Example correct output:
Welcome, traveler. The forest has been restless lately.

Example incorrect output:
*Elara smiles warmly* "Welcome, traveler," she says.
{"text": "Welcome, traveler.", "emotion": "Friendly"}"""

def load_persona(agent_id: str):
    """
    Load persona by agent_id.
    Searches in core/ first, then generic/.
    Falls back to default if not found.
    """
    agent_lower = agent_id.lower()
    
    search_paths = [
        os.path.join(PERSONAS_BASE_PATH, "core", f"{agent_lower}.yaml"),
        os.path.join(PERSONAS_BASE_PATH, "generic", f"{agent_lower}.yaml"),
    ]
    
    for path in search_paths:
        if os.path.exists(path):
            print(f"[Dialogue] Loading persona from: {path}")
            with open(path, 'r', encoding='utf-8') as f:
                return yaml.safe_load(f)
    
    default_path = os.path.join(PERSONAS_BASE_PATH, "core", "elara.yaml")
    print(f"[Dialogue] Persona '{agent_id}' not found, using default: {default_path}")
    if os.path.exists(default_path):
        with open(default_path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)
    
    return None

def dialogue_node(state: AgentState):
    """
    Dialogue Agent (Character) with RAG.
    Generates SpeakAction based on Persona, RAG Knowledge, and Memory using LLM.
    """
    # Get agent_id from intent (target_npc) or fallback to vr_context
    intent_data = state.get("analysis", {}).get("intent")
    vr_context = state.get("vr_context")
    agent_id = "Elara"  # Default
    
    # Priority: 1) target_npc from intent, 2) looking_at from vr_context
    if intent_data:
        if hasattr(intent_data, 'target_npc') and intent_data.target_npc:
            agent_id = intent_data.target_npc
        elif isinstance(intent_data, dict) and intent_data.get("target_npc"):
            agent_id = intent_data.get("target_npc")
    
    if agent_id == "Elara" and vr_context:  # Fallback
        if hasattr(vr_context, 'looking_at_entity_id') and vr_context.looking_at_entity_id:
            agent_id = vr_context.looking_at_entity_id
        elif isinstance(vr_context, dict) and vr_context.get("looking_at_entity_id"):
            agent_id = vr_context.get("looking_at_entity_id")
    
    print(f"[Dialogue] Agent ID: {agent_id}")
    
    # Load persona
    persona = load_persona(agent_id)
    if not persona:
        return {"next": "Error", "messages": ["System: Persona not found"]}
        
    memory = persona.get("memory_summary", {})
    key_events = memory.get("key_events", [])
    sentiment = memory.get("sentiment", "Neutral")
    
    # Get user input
    user_input = intent_data.raw_query if intent_data and hasattr(intent_data, 'raw_query') else "..."
    
    # --- RAG: Retrieve relevant knowledge ---
    rag_context = retrieve_context(agent_id, user_input, k=3)
    if rag_context:
        print(f"[Dialogue] RAG retrieved context for {agent_id}")
    else:
        print(f"[Dialogue] No RAG context available for {agent_id}")
    
    # --- Memory: Get recent chat history ---
    chat_history = get_conversation_context(agent_id, k=5)
    if chat_history:
        print(f"[Dialogue] Retrieved chat history for {agent_id}")
    
    # Check fallback mode
    current_speaker = state.get("current_speaker")
    messages = state.get("messages", [])
    fallback_reason = ""
    if current_speaker == "Supervisor_Fallback" and messages:
        fallback_reason = f" (Context: {messages[-1]})"
    
    # LLM Construction - Select model based on NPC importance
    npc_importance = persona.get('importance', 'normal')  # extra, normal, core
    llm = get_dialogue_llm(importance=npc_importance, temperature=0.7)
    print(f"[Dialogue] Using model for importance: {npc_importance}")

    # Build persona context with RAG
    persona_name = persona.get('name', 'Unknown')
    persona_role = persona.get('role', 'Unknown')
    persona_traits = ', '.join(persona.get('traits', []))
    memory_summary = '; '.join(key_events) if key_events else 'None'
    
    system_content = f"""{DIALOGUE_SYSTEM_PROMPT}

--- Character Info ---
Name: {persona_name}
Role: {persona_role}
Traits: {persona_traits}
Memory Summary: {memory_summary}
Sentiment toward Player: {sentiment}

--- Relevant Knowledge (use naturally) ---
{rag_context if rag_context else "No specific knowledge retrieved."}

--- Recent Conversation History ---
{chat_history if chat_history else "No previous conversation."}
"""
    
    human_content = f"Player said: '{user_input}'"
    if fallback_reason:
        human_content += f"\n[System Note]: The player's previous action was rejected. Reason: {fallback_reason}. Explain this in character."

    # Plain text generation (Gemma 3 doesn't support structured output)
    prompt = ChatPromptTemplate.from_messages([
        ("system", "{system_message}"),
        ("human", "{human_message}")
    ])
    
    try:
        chain = prompt | llm
        response = chain.invoke({
            "system_message": system_content,
            "human_message": human_content
        })
        
        # Extract text from response
        response_text = response.content if hasattr(response, 'content') else str(response)
        response_text = response_text.strip()
        
        # Remove any quotation marks if present
        if response_text.startswith('"') and response_text.endswith('"'):
            response_text = response_text[1:-1]
        
        print(f"[Dialogue] Generated response: {response_text[:50]}...")
        
        # Create SpeakAction manually
        speak_action = SpeakAction(text=response_text, emotion="Neutral")
        
        batch = ActionBatch(
            agent_id=persona_name,
            actions=[speak_action]
        )
        
        # --- Save conversation to memory ---
        try:
            add_conversation(agent_id, user_input, response_text)
            print(f"[Dialogue] Saved conversation to memory for {agent_id}")
        except Exception as mem_e:
            print(f"[Dialogue] Failed to save memory: {mem_e}")
            
    except Exception as e:
        print(f"LLM Logic Error in Dialogue: {e}")
        batch = ActionBatch(
            agent_id=persona_name,
            actions=[SpeakAction(text="...", emotion="Neutral")]
        )

    return {
        "action_batch": batch,
        "current_speaker": "Dialogue",
        "next": "End" 
    }
