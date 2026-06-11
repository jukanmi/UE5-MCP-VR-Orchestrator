"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: dialogue.py                                                           ║
║ Role: CREATIVE RESPONSE GENERATOR (Multi-NPC 3-Stage Pipeline)             ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Generate natural, in-character NPC responses based on persona, context,   ║
║   and conversational history. Outputs free-form text with speech, actions,  ║
║   and emotions - NOT structured data.                                       ║
║                                                                              ║
║ PIPELINE (3-Stage):                                                          ║
║   Stage 1: e4b × N 병렬 — NPC별 독립 호출 (지식 오염 없음)                 ║
║   Stage 2: 12B × 1 정제 — 스타일 향상만, 사실 추가 금지 (NPC 2개 이상 시) ║
║   Stage 3: interface_output.py 에서 ActionBatch 구조화                     ║
║                                                                              ║
║ INPUT:  target_npcs (List[str]), natural_context (str)                      ║
║ OUTPUT: raw_responses (Dict[str, str])  npc_id → refined raw text          ║
║                                                                              ║
║ OUTPUT FORMAT (CRITICAL):                                                   ║
║   "Speech in quotes" (emotion in parentheses) *physical action in asterisks*║
║                                                                              ║
║ LLM SELECTION:                                                               ║
║   Stage 1 — e4b (경량, 병렬 VRAM 효율)                                     ║
║   Stage 2 — gemma4-12b (품질 정제, 단일 호출)                              ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""

import yaml
import os
import re
import asyncio
from typing import Dict
from ...utils.llm_factory import get_llm, call_ollama_direct
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState
from ...utils import db_manager


PERSONAS_BASE_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "personas"
)

_memory_write_tasks: set = set()


def _on_memory_task_done(task) -> None:
    _memory_write_tasks.discard(task)
    if not task.cancelled() and task.exception():
        print(f"[Dialogue] 메모리 기록 백그라운드 실패: {task.exception()}")


DIALOGUE_SYSTEM_PROMPT = """You are an NPC in a VR game. Reply in character following the exact format below.

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
- Stay in character. Max 2-3 sentences of speech.

YOUR CHARACTER:
You are {name}, a {role}.
Personality traits: {traits}

Recent memory: {memory}
Current sentiment toward player: {sentiment}

Relevant context: {rag_context}
Conversation history: {chat_history}"""

REFINE_SYSTEM_PROMPT = """You are a quality editor for NPC dialogue in a VR game.
Your ONLY job: improve language style, fluency, and naturalness.

STRICT RULES:
- Do NOT add any new facts, knowledge, or lore not already present.
- Do NOT change which NPC says what — preserve the === NPC: <id> === section headers exactly.
- Preserve ALL [Mode:], [Facial:], [Action:] tags exactly as written.
- Keep speech within "double quotes", emotions in (parentheses).
- Max 2-3 sentences of speech per NPC.
- If a response is already good, return it unchanged.

Input format:
=== NPC: <id> ===
<raw response>

Output format: same section structure."""


def load_persona(agent_id: str):
    agent_lower = agent_id.lower()
    search_paths = [
        os.path.join(PERSONAS_BASE_PATH, "core", f"{agent_lower}.yaml"),
        os.path.join(PERSONAS_BASE_PATH, "generic", f"{agent_lower}.yaml"),
    ]
    for path in search_paths:
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                return yaml.safe_load(f)
    return _create_persona(agent_id)


def _create_persona(agent_id: str) -> dict:
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
        with open(save_path, "w", encoding="utf-8") as f:
            yaml.dump(persona, f, allow_unicode=True, default_flow_style=False)
        print(f"[Dialogue] Persona '{agent_id}' not found → created: {save_path}")
    except Exception as e:
        print(f"[Dialogue] Persona 파일 생성 실패: {e}")
    return persona


async def _dialogue_single(state: AgentState, npc_id: str) -> tuple[str, str]:
    """
    Stage 1: 단일 NPC에 대한 e4b 호출.
    반환: (npc_id, raw_response)
    지식 격리: 각 NPC의 persona/RAG/history를 독립적으로 로드.
    """
    natural_context = state.get("natural_context", "")

    persona = load_persona(npc_id) or {"name": npc_id, "importance": "normal"}
    persona_name = persona.get("name", npc_id)
    persona_role = persona.get("role", "Inhabitant")
    persona_traits = ", ".join(persona.get("traits", []))

    memory = persona.get("memory_summary", {})
    memory_summary = (
        "; ".join(memory.get("key_events", [])) if memory.get("key_events") else "None"
    )

    vr_context = state.get("vr_context")
    player_id = (
        vr_context.get("player_id", "Player")
        if isinstance(vr_context, dict)
        else getattr(vr_context, "player_id", "Player")
    )

    try:
        relation = await db_manager.get_affinity(npc_id, player_id)
        sentiment = f"{relation.reputation_tag} (Score: {relation.affinity_score})"
    except Exception as e:
        print(f"[Dialogue] Affinity 조회 실패 ({npc_id}): {e}")
        sentiment = memory.get("sentiment", "Neutral")

    clean_query = ""
    if vr_context:
        if hasattr(vr_context, "voice_transcript"):
            clean_query = vr_context.voice_transcript
        elif isinstance(vr_context, dict):
            clean_query = vr_context.get("voice_transcript", "")

    rag_context = (
        await asyncio.to_thread(retrieve_context, npc_id, clean_query, 3)
        if clean_query
        else ""
    )
    chat_history = get_conversation_context(npc_id, k=5)

    system_content = DIALOGUE_SYSTEM_PROMPT.format(
        name=persona_name,
        role=persona_role,
        traits=persona_traits,
        memory=memory_summary,
        sentiment=sentiment,
        rag_context=rag_context if rag_context else "None",
        chat_history=chat_history if chat_history else "No previous conversation",
    )

    print(f"[Dialogue] Stage1 e4b: {persona_name} | '{natural_context[:50]}...'")

    raw_response = None
    try:
        llm = get_llm(model_name="gemma4_slm", temperature=0.7, num_predict=300)
        prompt = ChatPromptTemplate.from_messages(
            [("system", "{system_msg}"), ("human", "Context: {context}")]
        )
        response = await (prompt | llm).ainvoke(
            {"system_msg": system_content, "context": natural_context}
        )
        raw_response = (
            response.content if hasattr(response, "content") else str(response)
        )
        raw_response = raw_response.strip()
        print(f"[Dialogue] Stage1 응답 ({npc_id}): '{raw_response[:60]}...'")
    except Exception as e:
        print(f"[Dialogue] Stage1 LLM 오류 ({npc_id}): {e}")
        cli_prompt = f"{system_content}\n\nContext: {natural_context}\n\nRespond in character now:"
        raw_response = await asyncio.to_thread(call_ollama_direct, cli_prompt, False)
        if raw_response:
            raw_response = raw_response.strip()

    if not raw_response:
        print(f"[Dialogue] Stage1 실패 ({npc_id}), 기본 응답 사용")
        raw_response = '[Mode: Social] [Facial: Neutral]\n"..." (confused) *looks at the player silently*'

    # fire-and-forget 메모리 기록
    clean_for_memory = re.sub(
        r"\[Mode:\s*\w+\]\s*\[Facial:\s*\w+\]\s*\n?",
        "",
        raw_response,
        flags=re.IGNORECASE,
    ).strip()
    speech_parts = re.findall(r'"([^"]+)"', clean_for_memory)
    speech_for_memory = speech_parts[0] if speech_parts else clean_for_memory[:100]
    memory_input = clean_query if clean_query else natural_context

    task = asyncio.create_task(
        asyncio.to_thread(add_conversation, npc_id, memory_input, speech_for_memory)
    )
    _memory_write_tasks.add(task)
    task.add_done_callback(_on_memory_task_done)

    return npc_id, raw_response


async def _refine_responses(raw_responses: Dict[str, str]) -> Dict[str, str]:
    """
    Stage 2: 12B 모델로 전체 NPC 응답 스타일 정제.
    사실 추가 금지 — 스타일/유창성 향상만.
    NPC 2개 이상일 때만 호출.
    """
    sections = "\n\n".join(
        f"=== NPC: {npc_id} ===\n{raw}" for npc_id, raw in raw_responses.items()
    )

    print(f"[Dialogue] Stage2 12B 정제 시작 ({len(raw_responses)}개 NPC)")
    refined_text = None
    try:
        llm = get_llm(
            model_name="gemma4", temperature=0.3, num_predict=500 * len(raw_responses)
        )
        prompt = ChatPromptTemplate.from_messages(
            [("system", REFINE_SYSTEM_PROMPT), ("human", "{sections}")]
        )
        response = await (prompt | llm).ainvoke({"sections": sections})
        refined_text = (
            response.content if hasattr(response, "content") else str(response)
        )
        refined_text = refined_text.strip()
        print(f"[Dialogue] Stage2 정제 완료 ({len(refined_text)} chars)")
    except Exception as e:
        print(f"[Dialogue] Stage2 12B 오류, 원본 유지: {e}")
        return raw_responses

    # 섹션 구분자로 파싱
    refined: Dict[str, str] = {}
    for npc_id in raw_responses:
        pattern = rf"===\s*NPC:\s*{re.escape(npc_id)}\s*===\s*\n(.*?)(?===\s*NPC:|$)"
        m = re.search(pattern, refined_text, re.DOTALL | re.IGNORECASE)
        if m:
            refined[npc_id] = m.group(1).strip()
        else:
            print(f"[Dialogue] Stage2 파싱 실패 ({npc_id}), 원본 유지")
            refined[npc_id] = raw_responses[npc_id]

    return refined


async def dialogue_node(state: AgentState):
    """
    Dialogue Agent (3-Stage Multi-NPC).

    Stage 1: e4b × N 병렬 (지식 격리, 각 NPC 독립 호출)
    Stage 2: 12B × 1 정제 (NPC 2개 이상 시만)
    Output:  raw_responses Dict[npc_id, str]
    """
    npcs = state.get("target_npcs") or []
    if not npcs:
        single = state.get("target_npc", "Elara")
        npcs = [single] if single else ["Elara"]

    print(f"[Dialogue] 대상 NPC: {npcs}")

    # Stage 1: 병렬 e4b 호출
    results = await asyncio.gather(
        *[_dialogue_single(state, npc_id) for npc_id in npcs]
    )
    raw_responses: Dict[str, str] = dict(results)

    # Stage 2: 12B 정제 (멀티 NPC 시만)
    if len(npcs) > 1:
        raw_responses = await _refine_responses(raw_responses)

    # 단일 NPC 호환: raw_response 도 채움
    single_npc = npcs[0]
    raw_response = raw_responses.get(single_npc, "")

    return {
        "raw_responses": raw_responses,
        "raw_response": raw_response,
        "target_npc": single_npc,
        "current_speaker": "Dialogue",
        "next": "Interface_Output",
    }
