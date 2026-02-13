"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: dialogue.py                                                           ║
║ Role: CREATIVE RESPONSE GENERATOR (LLM #2 - Core Intelligence)             ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Generate natural, in-character NPC responses based on persona, context,   ║
║   and conversational history. Outputs free-form text with speech, actions,  ║
║   and emotions - NOT structured data.                                       ║
║                                                                              ║
║ INPUT:  natural_context (str) - What player wants/said                      ║
║ OUTPUT: raw_response (str) - Free-form NPC reaction                         ║
║                                                                              ║
║ OUTPUT FORMAT (CRITICAL):                                                   ║
║   "Speech in quotes" (emotion in parentheses) *physical action in asterisks*║
║                                                                              ║
║ PERSONA SYSTEM:                                                              ║
║   - Loads YAML persona files (name, role, traits, memory)                   ║
║   - Uses RAG for contextual knowledge retrieval                             ║
║   - Maintains conversation history via memory_manager                       ║
║                                                                              ║
║ LLM SELECTION:                                                               ║
║   - High importance NPCs → Gemma 3 API (premium quality)                    ║
║   - Normal NPCs → Gemini CLI (cost-effective)                               ║
║                                                                              ║
║ EXAMPLE:                                                                     ║
║   IN:  "Player is pointing at door and asking to open it"                   ║
║   OUT: "Of course! (cheerfully) *walks to door and opens it*"               ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import yaml
import os
import json
from ...utils.llm_factory import get_dialogue_llm, call_gemini_cli
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState


PERSONAS_BASE_PATH = "app/agents/personas"


# System Prompt for free-form response generation
DIALOGUE_SYSTEM_PROMPT = """You are {name}, a {role}.
Personality traits: {traits}

Recent memory: {memory}
Current sentiment toward player: {sentiment}

Relevant context: {rag_context}
Conversation history: {chat_history}

RESPONSE FORMAT (CRITICAL - follow exactly):
1. FIRST LINE: [Mode: <mode>] [Facial: <expression>]
   - Mode MUST be one of: Combat, Social, Task, Investigation, Lifestyle
   - Facial MUST be one of: Neutral, Happy, Sad, Angry, Fear, Surprised, Disgusted, Tired, Pain

2. SECOND LINE ONWARDS: Your natural response
   - Use "double quotes" for everything you SAY out loud
   - Use *asterisks* for PHYSICAL ACTIONS you perform (movement, combat, interaction)
   - Use (parentheses) for your EMOTION or tone

RESPONSE EXAMPLES:
[Mode: Social] [Facial: Happy]
"Of course, I'll open it!" (cheerfully) *walks to the door and opens it*

[Mode: Combat] [Facial: Angry]
"Stay back!" (furiously) *draws sword and attacks the enemy*

[Mode: Social] [Facial: Sad]
"I understand." (sadly) *nods slowly*

[Mode: Investigation] [Facial: Surprised]
"What's that sound?" (alarmed) *turns toward the noise*

[Mode: Lifestyle] [Facial: Tired]
"I need rest..." (exhausted) *sits down on the bench*

MODE SELECTION GUIDE:
- Combat: Fighting, defending, fleeing from danger
- Social: Talking, trading, following, emotional interaction
- Task: Picking up items, using objects, crafting, eating
- Investigation: Searching, tracking, observing, scouting
- Lifestyle: Sitting, sleeping, reading, idle activities

FACIAL EXPRESSION GUIDE:
- Neutral: Default, calm state
- Happy: Joy, satisfaction, friendliness
- Sad: Sorrow, disappointment, grief
- Angry: Rage, frustration, hostility
- Fear: Terror, anxiety, panic
- Surprised: Shock, amazement, confusion
- Disgusted: Revulsion, contempt, distaste
- Tired: Exhaustion, fatigue, weariness
- Pain: Physical suffering, injury

RULES:
- ALWAYS start with [Mode: X] [Facial: Y] on the first line
- Always include speech in "quotes"
- Always include at least one emotion in (parentheses)
- Include *physical actions* when the context implies movement or interaction
- Stay in character based on your personality traits
- Max 2-3 sentences of speech
- Be natural and expressive"""


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

    # Fallback to default
    default_path = os.path.join(PERSONAS_BASE_PATH, "core", "elara.yaml")
    print(f"[Dialogue] Persona '{agent_id}' not found, using default: {default_path}")
    if os.path.exists(default_path):
        with open(default_path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)
            
    return None


def dialogue_node(state: AgentState):
    """
    Dialogue Agent (LLM #2).
    
    Generates free-form NPC response using persona and context.
    Does NOT create ActionBatch — that's Interface Output's job.
    
    Input: AgentState with natural_context (str) and target_npc (str)
    Output: AgentState with raw_response (str)
    """
    natural_context = state.get("natural_context", "")
    target_npc = state.get("target_npc", "Elara")
    
    # --- Legacy compatibility: fall back to old analysis/intent flow ---
    if not natural_context:
        analysis = state.get("analysis", {})
        intent_data = analysis.get("intent")
        if intent_data:
            user_input = getattr(intent_data, 'raw_query', None)
            if isinstance(intent_data, dict):
                user_input = intent_data.get("raw_query", "")
                target_npc = intent_data.get("target_npc", target_npc)
            elif hasattr(intent_data, 'target_npc') and intent_data.target_npc:
                target_npc = intent_data.target_npc
            natural_context = f"Player said: \"{user_input}\""
    
    # Determine agent ID
    agent_id = target_npc
    
    # Fallback to vr_context looking_at
    if agent_id == "Elara":
        vr_context = state.get("vr_context")
        if vr_context:
            looking_at = None
            if hasattr(vr_context, 'looking_at_entity_id'):
                looking_at = vr_context.looking_at_entity_id
            elif isinstance(vr_context, dict):
                looking_at = vr_context.get("looking_at_entity_id")
            if looking_at:
                agent_id = looking_at

    # Load persona
    persona = load_persona(agent_id) or {"name": agent_id, "importance": "normal"}
    persona_name = persona.get('name', agent_id)
    persona_role = persona.get('role', 'Inhabitant')
    persona_traits = ', '.join(persona.get('traits', []))
    
    memory = persona.get("memory_summary", {})
    memory_summary = '; '.join(memory.get('key_events', [])) if memory.get('key_events') else 'None'
    sentiment = memory.get("sentiment", "Neutral")

    # Extract user input for RAG/memory
    user_input = natural_context

    # Retrieve context via RAG and memory
    rag_context = retrieve_context(agent_id, user_input, k=3)
    chat_history = get_conversation_context(agent_id, k=5)

    # Build system prompt with persona info
    system_content = DIALOGUE_SYSTEM_PROMPT.format(
        name=persona_name,
        role=persona_role,
        traits=persona_traits,
        memory=memory_summary,
        sentiment=sentiment,
        rag_context=rag_context if rag_context else "None",
        chat_history=chat_history if chat_history else "No previous conversation"
    )

    print(f"[Dialogue] Agent: {persona_name} | Context: '{natural_context[:60]}...'")

    raw_response = None

    # --- Choose LLM based on NPC importance ---
    importance = persona.get('importance', 'normal')
    
    if importance in ['high', 'core']:
        # High importance NPC → Use Gemma 3 API for quality
        print(f"[Dialogue] Using Gemma 3 API (importance: {importance})")
        
        try:
            llm = get_dialogue_llm(importance=importance, temperature=0.7)
            prompt = ChatPromptTemplate.from_messages([
                ("system", "{system_msg}"),
                ("human", "Context: {context}")
            ])
            
            response = (prompt | llm).invoke({
                "system_msg": system_content,
                "context": natural_context
            })
            raw_response = response.content if hasattr(response, 'content') else str(response)
            raw_response = raw_response.strip()
            print(f"[Dialogue] Gemma 3 response: '{raw_response[:80]}...'")
        except Exception as e:
            print(f"[Dialogue] Gemma 3 Error: {e}")
    
    if not raw_response:
        # Normal NPC or API failed → Use Gemini CLI
        print(f"[Dialogue] Using Gemini CLI (importance: {importance})")
        
        cli_prompt = f"{system_content}\n\nContext: {natural_context}\n\nRespond in character now:"
        raw_response = call_gemini_cli(cli_prompt, extract_json=False)
        
        if raw_response:
            raw_response = raw_response.strip()
            print(f"[Dialogue] CLI response: '{raw_response[:80]}...'")
    
    # Fallback if everything fails
    if not raw_response:
        print("[Dialogue] All LLMs failed, using fallback response")
        raw_response = '[Mode: Social] [Facial: Neutral]\n"..." (confused) *looks at the player silently*'

    # --- Parse Mode and FacialState from response ---
    import re
    
    mode = "Social"  # Default
    facial_state = "Neutral"  # Default
    
    # Extract [Mode: X] and [Facial: Y] from first line
    mode_match = re.search(r'\[Mode:\s*(\w+)\]', raw_response, re.IGNORECASE)
    facial_match = re.search(r'\[Facial:\s*(\w+)\]', raw_response, re.IGNORECASE)
    
    if mode_match:
        mode = mode_match.group(1)
        print(f"[Dialogue] Parsed Mode: {mode}")
    else:
        print(f"[Dialogue] WARNING: No Mode found in response, using default: {mode}")
    
    if facial_match:
        facial_state = facial_match.group(1)
        print(f"[Dialogue] Parsed FacialState: {facial_state}")
    else:
        print(f"[Dialogue] WARNING: No FacialState found in response, using default: {facial_state}")
    
    # Remove the [Mode: X] [Facial: Y] line from raw_response for cleaner output
    raw_response_clean = re.sub(r'\[Mode:\s*\w+\]\s*\[Facial:\s*\w+\]\s*\n?', '', raw_response, flags=re.IGNORECASE).strip()

    # Save to conversation memory
    # Extract just the speech part for memory
    speech_parts = re.findall(r'"([^"]+)"', raw_response_clean)
    speech_for_memory = speech_parts[0] if speech_parts else raw_response_clean[:100]
    add_conversation(agent_id, user_input, speech_for_memory)

    return {
        "raw_response": raw_response_clean,
        "behavior_mode": mode,
        "facial_state": facial_state,
        "target_npc": persona_name,
        "current_speaker": "Dialogue",
        "next": "Interface_Output"
    }
