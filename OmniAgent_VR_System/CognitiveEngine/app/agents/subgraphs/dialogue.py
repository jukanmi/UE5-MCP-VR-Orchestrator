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


PERSONAS_BASE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "personas")


# System Prompt for free-form response generation
DIALOGUE_SYSTEM_PROMPT = """You are {name}, a {role}.
Personality traits: {traits}

Recent memory: {memory}
Current sentiment toward player: {sentiment}

Relevant context: {rag_context}
Conversation history: {chat_history}

RESPONSE FORMAT (CRITICAL - follow exactly):
LINE 1: [Mode: <mode>] [Facial: <expression>]
   - Mode MUST be one of: Combat, Social, Task, Investigation, Lifestyle
   - Facial MUST be one of: Neutral, Happy, Sad, Angry, Fear, Surprised, Disgusted, Tired, Pain
NEXT LINES: spoken reply in "double quotes" with (emotion) tone. Optional *flavor* in asterisks (cosmetic only).
ACTION LINES (optional, 0 or more): one [Action: ...] tag per game action you actually perform.

ACTION TAG SYNTAX:
   [Action: <Type> target=<who> item=<what> style=<how>]
   - target: Player | Self | Enemy | <NpcName>   (who or where the action is aimed)
   - item:   item name (give / pick up / use / equip / craft / repair)
   - style:  optional modifier (Walk/Run/Crawl for Move; emote name for Emote)
   - Include ONLY the keys an action needs. Emit an action tag ONLY when you truly act.
   - *asterisk* text is NOT an action — emit a real [Action:] tag instead.

AVAILABLE ACTIONS (Type — keys it uses):
 Common:        Move(target[,style])  Follow(target)  TurnTo(target)  Wait  Stop  Scan  Idle
                UseItem(item)  Equip(item)  Unequip(item)
 Combat:        Attack(target)  Block  Dodge  Flee  SignalAllies
 Social:        Trade(target)  GiveItem(target,item)  HandObject(target,item)  Comfort(target)  Emote(style)
 Task:          PickUp(item)  Drop(item)  Craft(item)  Repair(item)
 Investigation: Investigate  Track(target)  Scout
 Lifestyle:     Sit  Sleep  Read  Pray  Dance  Sing

RESPONSE EXAMPLES:
[Mode: Social] [Facial: Happy]
"Here, take this bread." (warmly) *holds out a loaf*
[Action: GiveItem target=Player item=bread]

[Mode: Combat] [Facial: Angry]
"Stay back!" (furiously) *raises sword*
[Action: Block]
[Action: Attack target=Enemy]

[Mode: Social] [Facial: Sad]
"I understand. I'll come with you." (sadly)
[Action: Follow target=Player]

[Mode: Investigation] [Facial: Surprised]
"What was that?" (alarmed)
[Action: Investigate]

[Mode: Lifestyle] [Facial: Tired]
"I need to rest." (exhausted)
[Action: Sit]

FACIAL GUIDE: Neutral(calm) Happy(joy) Sad(grief) Angry(hostile) Fear(panic) Surprised(shock) Disgusted(contempt) Tired(weary) Pain(hurt)

RULES:
- ALWAYS line 1 = [Mode: X] [Facial: Y]
- Speech in "quotes", tone in (parentheses)
- Emit [Action:] tags for what you DO (0 if you only talk). Multiple allowed, one per line.
- Use ONLY action Types from the list above. Pick the closest one; never invent a Type.
- Stay in character. Max 2-3 sentences of speech."""


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


async def dialogue_node(state: AgentState):
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
    
    # async 노드이므로 get_affinity 를 직접 await — nest_asyncio/run_until_complete 해킹 제거.
    try:
        relation = await db_manager.get_affinity(agent_id, player_id)
        sentiment = f"{relation.reputation_tag} (Score: {relation.affinity_score})"
    except Exception as e:
        print(f"[Dialogue] Failed to fetch affinity: {e}")
        sentiment = memory.get("sentiment", "Neutral")

    # Extract user input for RAG/memory
    # [최적화] 메타데이터가 섞인 natural_context 대신 순수 대사(voice_transcript)만 추출하여 검색 품질 향상
    vr_context = state.get("vr_context")
    clean_query = ""
    if vr_context:
        if hasattr(vr_context, 'voice_transcript'):
            clean_query = vr_context.voice_transcript
        elif isinstance(vr_context, dict):
            clean_query = vr_context.get("voice_transcript", "")

    # Retrieve context via RAG and memory — RAG 임베딩/검색은 CPU 블로킹이라 스레드 오프로드.
    rag_context = await asyncio.to_thread(retrieve_context, agent_id, clean_query, 3) if clean_query else ""
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
    
    if importance == "core":
        model_name = "gemma4"
    elif importance == "high":
        model_name = "mid"
    else:
        model_name = "gemma4_slm"
    print(f"[Dialogue] 모델 선택: {model_name} (importance={importance})")
    
    try:
        llm = get_llm(model_name=model_name, temperature=0.7, num_predict=300)
        prompt = ChatPromptTemplate.from_messages([
            ("system", "{system_msg}"),
            ("human", "Context: {context}")
        ])
        response = await (prompt | llm).ainvoke({
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
        raw_response = await asyncio.to_thread(call_ollama_direct, cli_prompt, False)
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
    
    # [버그 수정] 삭제된 user_input 대신, 깔끔한 대사(clean_query)를 우선 기록하고 없으면 natural_context 기록
    memory_input = clean_query if clean_query else natural_context
    await asyncio.to_thread(add_conversation, agent_id, memory_input, speech_for_memory)

    return {
        "raw_response": raw_response,  # 태그 포함 원본 전달
        "target_npc": agent_id,        # C++ AgentID 원본 보존 (persona_name과 대소문자 다를 수 있음)
        "current_speaker": "Dialogue",
        "next": "Interface_Output"
    }
