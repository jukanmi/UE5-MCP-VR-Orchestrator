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
import json
from ...utils.llm_factory import get_dialogue_llm, call_gemini_cli
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState
from ...schemas.actions import ActionBatch, SpeakAction, GameAction

PERSONAS_BASE_PATH = "app/agents/personas"

# SYSTEM PROMPT for CLI Actions
ACTION_SYSTEM_PROMPT = """Role: Action Generator
Valid GameAction Types: Move, Attack, Interact, Emote, Wait.
Output ONLY JSON matching: {"action_type": "string", "target_id": "string", "parameters": {"key": "value"}}
"""

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
            with open(path, 'r', encoding='utf-8') as f:
                return yaml.safe_load(f)
    # Fallback to default if not found
    default_path = os.path.join(PERSONAS_BASE_PATH, "core", "elara.yaml")
    print(f"[Dialogue] Persona '{agent_id}' not found, using default: {default_path}")
    if os.path.exists(default_path):
        with open(default_path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)
            
    return None

def dialogue_node(state: AgentState):
    """
    Combined Brain: 
    - Speech -> Gemma 3 (API)
    - Others -> Gemini CLI (Quota Saving)
    """
    analysis = state.get("analysis", {})
    intent_data = analysis.get("intent")
    vr_context = state.get("vr_context")
    
    # 1. Identity & Context
    agent_id = "Elara" # Default
    if intent_data:
        if hasattr(intent_data, 'target_npc') and intent_data.target_npc:
            agent_id = intent_data.target_npc
        elif isinstance(intent_data, dict) and intent_data.get("target_npc"):
            agent_id = intent_data.get("target_npc")
    
    # Fallback to what player is looking at if agent_id is default
    if agent_id == "Elara" and vr_context:
        if hasattr(vr_context, 'looking_at_entity_id') and vr_context.looking_at_entity_id:
            agent_id = vr_context.looking_at_entity_id
        elif isinstance(vr_context, dict) and vr_context.get("looking_at_entity_id"):
            agent_id = vr_context.get("looking_at_entity_id")

    persona = load_persona(agent_id) or {"name": agent_id, "importance": "normal"}
    persona_name = persona.get('name', agent_id)
    persona_role = persona.get('role', 'Inhabitant')
    persona_traits = ', '.join(persona.get('traits', []))
    
    memory = persona.get("memory_summary", {})
    memory_summary = '; '.join(memory.get('key_events', [])) if memory.get('key_events') else 'None'
    sentiment = memory.get("sentiment", "Neutral")

    user_input = intent_data.raw_query if intent_data and hasattr(intent_data, 'raw_query') else "..."
    if isinstance(intent_data, dict):
        action_type = intent_data.get("action_type", "Unknown")
    else:
        action_type = intent_data.action_type if intent_data and hasattr(intent_data, 'action_type') else "Unknown"

    final_actions = []
    reasoning = []

    # --- Common Context Retrieval ---
    rag_context = retrieve_context(agent_id, user_input, k=3)
    chat_history = get_conversation_context(agent_id, k=5)

    # --- PATH A: Speech (Gemma 3 API) ---
    if action_type in ["Speak", "Chat", "Talk", "Unknown"]:
        llm = get_dialogue_llm(importance=persona.get('importance', 'normal'), temperature=0.7)
        
        system_content = f"""You are {persona_name}, a {persona_role}.
Traits: {persona_traits}
Memory: {memory_summary}
Sentiment: {sentiment}

Relevant Knowledge: {rag_context if rag_context else "None"}
History: {chat_history if chat_history else "No history"}

Task: Respond in character naturally. Max 2-3 sentences. No narration."""
        
        prompt = ChatPromptTemplate.from_messages([
            ("system", "{system_msg}"),
            ("human", "Player: {user_input}")
        ])
        
        try:
            response = (prompt | llm).invoke({
                "system_msg": system_content,
                "user_input": user_input
            })
            response_text = response.content if hasattr(response, 'content') else str(response)
            clean_text = response_text.strip().replace('"', '')
            final_actions.append(SpeakAction(text=clean_text, emotion="Neutral"))
            add_conversation(agent_id, user_input, clean_text)
            reasoning.append("Gemma3 Speech Generated")
        except Exception as e:
            print(f"Speech Error: {e}")

    # --- PATH B: Game Actions (Gemini CLI) ---
    if action_type not in ["Speak", "Chat", "Talk", "Wait", "Unknown"]:
        intent_json = json.dumps(intent_data.model_dump() if hasattr(intent_data, 'model_dump') else intent_data)
        
        # Inject context into CLI prompt for smarter actions
        context_block = f"Context:\n- History: {chat_history}\n- Knowledge: {rag_context}"
        cli_prompt = f"{ACTION_SYSTEM_PROMPT}\n\n{context_block}\n\nConvert this Intent to JSON:\n{intent_json}"
        
        cli_result = call_gemini_cli(cli_prompt)
        if cli_result:
            try:
                data = json.loads(cli_result)
                if "parameters" in data and isinstance(data["parameters"], dict):
                    data["parameters"] = {str(k): str(v) for k, v in data["parameters"].items()}
                final_actions.append(GameAction(**data))
                reasoning.append(f"CLI Resolved action: {action_type}")
            except Exception as e:
                print(f"CLI Action Parse Error: {e}")

    # 3. Final Packaging
    batch = ActionBatch(
        agent_id=persona_name,
        actions=final_actions or [SpeakAction(text="...", emotion="Neutral")],
        reasoning="; ".join(reasoning)
    )

    return {
        "action_batch": batch,
        "current_speaker": "Dialogue",
        "next": "Rules"
    }
