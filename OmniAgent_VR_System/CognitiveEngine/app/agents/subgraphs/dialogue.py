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
║   - Normal NPCs → Ollama (qwen)                                             ║
║                                                                              ║
║ EXAMPLE:                                                                     ║
║   IN:  "Player is pointing at door and asking to open it"                   ║
║   OUT: "Of course! (cheerfully) *walks to door and opens it*"               ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""
import yaml
import os
import json
from ...utils.llm_factory import get_llm, call_ollama_direct
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState
from ...utils import db_manager


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
    YAML이 없으면 generic/ 에 기본 파일을 자동 생성 후 반환.
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

    return _create_persona(agent_id)


def _create_persona(agent_id: str) -> dict:
    """
    알 수 없는 NPC용 기본 페르소나를 생성하고 generic/ 에 저장한다.
    WHY: 등록되지 않은 NPC가 요청을 보낼 때 elara 페르소나로 응답하면
         완전히 다른 인물이 대답하는 문제가 생긴다.
         최소한의 정체성(이름, 중립 성격)을 부여해 일관성을 유지한다.
    """
    persona = {
        "name": agent_id,
        "importance": "normal",
        "role": "Inhabitant",
        "traits": ["Cautious", "Reserved", "Observant"],
        "memory_summary": {
            "key_events": [],
            "sentiment": "Neutral",
            "last_interaction_timestamp": 0,
        },
    }

    save_path = os.path.join(PERSONAS_BASE_PATH, "generic", f"{agent_id.lower()}.yaml")
    os.makedirs(os.path.dirname(save_path), exist_ok=True)
    try:
        with open(save_path, 'w', encoding='utf-8') as f:
            yaml.dump(persona, f, allow_unicode=True, default_flow_style=False)
        print(f"[Dialogue] Persona '{agent_id}' not found → created: {save_path}")
    except Exception as e:
        print(f"[Dialogue] Persona 파일 생성 실패: {e}")

    return persona


def dialogue_node(state: AgentState):
    """
    Dialogue Agent (LLM #2).
    
    Generates free-form NPC response using persona and context.
    Does NOT create ActionBatch — that's Interface Output's job.
    
    Input: AgentState with natural_context (str) and target_npc (str)
    Output: AgentState with raw_response (str)
    """
    import asyncio
    
    natural_context = state.get("natural_context", "")
    target_npc = state.get("target_npc", "Elara")
    
    # Determine agent ID
    agent_id = target_npc

    # Load persona
    persona = load_persona(agent_id) or {"name": agent_id, "importance": "normal"}
    persona_name = persona.get('name', agent_id)
    persona_role = persona.get('role', 'Inhabitant')
    persona_traits = ', '.join(persona.get('traits', []))
    
    memory = persona.get("memory_summary", {})
    memory_summary = '; '.join(memory.get('key_events', [])) if memory.get('key_events') else 'None'
    
    # --- Affinity DB 연동: 실제 대상(보통 Player)과의 호감도(Sentiment) 조회 ---
    player_id = state.get("vr_context", {}).get("player_id", "Player") if isinstance(state.get("vr_context"), dict) else getattr(state.get("vr_context"), "player_id", "Player")
    
    try:
        # LangGraph 콜백 등 쓰레드 문제 해결을 위해 async loop 없이 임시로 동기 메소드 활용 (또는 에러 방지 위해 우회)
        try:
            loop = asyncio.get_event_loop()
            if loop.is_running():
                import nest_asyncio
                nest_asyncio.apply()
            relation = loop.run_until_complete(db_manager.get_affinity(agent_id, player_id))
            sentiment = f"{relation.reputation_tag} (Score: {relation.affinity_score})"
        except RuntimeError:
            # Cannot use run_until_complete inside active loop without nest_asyncio trick failing in some contexts.
            # Fallback for now, relying on initial lookup or skipping.
            sentiment = memory.get("sentiment", "Neutral")
    except Exception as e:
        print(f"[Dialogue] Failed to fetch affinity: {e}")
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

    # --- LLM 선택 (importance에 따라 큐 또는 SLM 분기) ---
    importance = persona.get('importance', 'normal')
    
    # 최적화 3번: 무거운 70B(llama) 대신 26B(gemma4) 또는 8B(qwen) 사용
    model_name = "gemma4" if importance in ("high", "core") else "qwen"
    print(f"[Dialogue] 모델 선택: {model_name} (importance={importance})")
    
    try:
        llm = get_llm(model_name=model_name, temperature=0.7, num_predict=300)
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
        print(f"[Dialogue] 응답: '{raw_response[:80]}...'")
    except Exception as e:
        print(f"[Dialogue] LLM 오류 ({model_name}): {e}")
        # 폴백: call_ollama_direct 직접 호출
        cli_prompt = f"{system_content}\n\nContext: {natural_context}\n\nRespond in character now:"
        raw_response = call_ollama_direct(cli_prompt, extract_json=False)
        if raw_response:
            raw_response = raw_response.strip()

    # 모든 방법 실패 시 기본 응답
    if not raw_response:
        print("[Dialogue] 모든 LLM 실패, 기본 응답 사용")
        raw_response = '[Mode: Social] [Facial: Neutral]\n"..." (confused) *looks at the player silently*'

    # [Mode: X] [Facial: Y] 태그는 raw_response에 포함된 채로 전달.
    # 왜: interface_output.py가 모든 구조화(structuring)를 책임지므로
    # dialogue.py는 자연어 생성만 담당하고 파싱/변환은 하지 않는다.
    import re

    # 대화 기록 저장 (태그 제거 후 speech만 추출)
    clean_for_memory = re.sub(r'\[Mode:\s*\w+\]\s*\[Facial:\s*\w+\]\s*\n?', '', raw_response, flags=re.IGNORECASE).strip()
    speech_parts = re.findall(r'"([^"]+)"', clean_for_memory)
    speech_for_memory = speech_parts[0] if speech_parts else clean_for_memory[:100]
    add_conversation(agent_id, user_input, speech_for_memory)

    return {
        "raw_response": raw_response,  # 태그 포함 원본 전달
        "target_npc": agent_id,        # C++ AgentID 원본 보존 (persona_name과 대소문자 다를 수 있음)
        "current_speaker": "Dialogue",
        "next": "Interface_Output"
    }
