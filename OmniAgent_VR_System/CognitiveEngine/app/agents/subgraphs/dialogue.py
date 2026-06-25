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
from functools import lru_cache
from typing import Dict
from ...utils.llm_factory import get_llm, call_ollama_direct, ollama_structured
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from langchain_core.prompts import ChatPromptTemplate
from ..state import AgentState
from ...utils import db_manager
from ...schemas.actions import DialogueResponse


PERSONAS_BASE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "personas")

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
- ALWAYS reply in Korean (한국어로만 답변).

YOUR CHARACTER:
You are {name}, a {role}.
Personality traits: {traits}
Items you currently hold: {inventory} (only give/use items you actually have)

Recent memory: {memory}
Current sentiment toward player: {sentiment}

Relevant context: {rag_context}
Conversation history: {chat_history}"""

# 구조화 출력(with_structured_output) 전용 프롬프트. 텍스트 태그 포맷 대신 JSON 필드
# (speech/actions)를 채우도록 지시 — 텍스트포맷용 DIALOGUE_SYSTEM_PROMPT 를 그대로 쓰면
# e4b 가 스키마와 충돌해 speech/actions 를 비움(실측). 폴백(call_ollama_direct 자유텍스트)
# 경로는 여전히 DIALOGUE_SYSTEM_PROMPT 사용.
DIALOGUE_STRUCTURED_PROMPT = """You are {name}, a {role}, an NPC in a VR game.
Personality traits: {traits}

Respond ONLY as a JSON object with fields: mode, facial, speech, tone, actions, plan_achieved.
- speech: your spoken line, in character, 1-3 sentences in Korean (NEVER empty, ALWAYS Korean).
- tone: emotional tone of the speech (e.g. warmly, furiously).
- actions: list of game actions you perform RIGHT NOW. MANDATORY examples:
    player says "follow me" / "나 따라와" → actions=[{{"type":"Follow","target":"Player"}}]
    player asks you to move somewhere → actions=[{{"type":"Move","target":"<loc>"}}]
    combat situation → actions=[{{"type":"Attack","target":"Enemy"}}]
    give item → actions=[{{"type":"GiveItem","target":"Player","item":"<item>"}}]
  Empty list ONLY if you are purely talking with NO physical action implied.
- plan_achieved: true ONLY if the current plan goal is clearly completed this turn; otherwise false.

Available action types: Move Follow TurnTo Wait Stop Scan Idle UseItem Equip Unequip
 Attack Block Dodge Flee SignalAllies Trade GiveItem HandObject Comfort Emote
 PickUp Drop Craft Repair Investigate Track Scout Sit Sleep Read Pray Dance Sing.
Use ONLY a type from this list. target is one of: Player, Self, Enemy, or an NPC name.

YOUR inventory (items you currently hold): {inventory}
 - Only GiveItem/HandObject/UseItem/Equip an item that is in YOUR inventory above.
 - If asked for an item you do NOT have, say so — do NOT emit a give/use action for it.

Recent memory: {memory}
Current sentiment toward player: {sentiment}
Relevant context: {rag_context}
Conversation history: {chat_history}"""

REFINE_SYSTEM_PROMPT = """You are a quality editor and planner for NPC dialogue in a VR game.
Your jobs: (1) improve language style/fluency, (2) emit a short forward plan per NPC.

STRICT RULES:
- Do NOT add any new facts, knowledge, or lore not already present.
- Do NOT change which NPC says what — preserve the === NPC: <id> === section headers exactly.
- Preserve ALL [Mode:], [Facial:], [Action:] tags exactly as written.
- Keep speech within "double quotes", emotions in (parentheses).
- Max 2-3 sentences of speech per NPC.
- ALL speech must be in Korean (한국어로만 작성).

PLAN (CRITICAL):
- At the END of each NPC section, output EXACTLY ONE line:
  [Plan: goal=<short goal> | steps=<step1>;<step2>;<step3>]
- goal: one short phrase describing what this NPC is trying to achieve over the next few turns.
- steps: 2-4 concrete beats separated by ';', ordered. These guide later lightweight dialogue.
- The [Plan: ...] line is metadata, NOT spoken dialogue.

Input format:
=== NPC: <id> ===
<raw response>

Output format: same section structure, each section ending with one [Plan: ...] line."""


# persona 는 런타임 불변(_create_persona 만 최초 1회 기록, 이후 미수정) → 캐시.
# e4b 5-step 루프에서 NPC 당 매 스텝 YAML 디스크 로드하던 것을 1회로 축소.
# 호출부는 persona 를 읽기만(.get) 하므로 공유 dict 안전. 외부 파일 수정 시
# 프로세스 재시작 필요(런타임 핫리로드 없음 — 현 설계상 무관).
@lru_cache(maxsize=64)
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


# e4b/llama.cpp byte-fallback 토큰(<0xEC><0xB3><0x87> 같은)이 출력에 새는 경우 — 연속
# 바이트를 모아 UTF-8 로 재조합(예: "쳇"), 디코드 불가한 잔여만 제거.
_BYTE_TOKEN_RUN_RE = re.compile(r"(?:<0[xX][0-9A-Fa-f]{2}>)+")


def _decode_byte_tokens(text: str) -> str:
    if "<0x" not in text and "<0X" not in text:
        return text

    def _repl(m: "re.Match") -> str:
        hexes = re.findall(r"<0[xX]([0-9A-Fa-f]{2})>", m.group(0))
        try:
            return bytes(int(h, 16) for h in hexes).decode("utf-8")
        except UnicodeDecodeError:
            return ""  # 불완전/깨진 바이트열 → 제거

    return _BYTE_TOKEN_RUN_RE.sub(_repl, text)


def _serialize_dialogue(obj: DialogueResponse) -> str:
    """구조화 DialogueResponse → 기존 자유텍스트 포맷으로 직렬화.
    interface_output 정규식이 그대로 파싱하도록 [Mode:][Facial:]"speech"[Action:]
    형태 재생. 액션 태그 키(target/item/loc/style)는 _PARAM_KEY_MAP 기준."""
    lines = [f"[Mode: {obj.mode}] [Facial: {obj.facial}]"]

    speech = _decode_byte_tokens((obj.speech or "").strip())
    if speech:
        tone = _decode_byte_tokens((obj.tone or "").strip())
        lines.append(f'"{speech}" ({tone})' if tone else f'"{speech}"')

    for act in obj.actions:
        parts = [act.type]
        for key, val in (
            ("target", act.target),
            ("item", act.item),
            ("loc", act.loc),
            ("style", act.style),
        ):
            v = (val or "").strip()
            if v:
                parts.append(f"{key}={v}")
        lines.append(f"[Action: {' '.join(parts)}]")

    return "\n".join(lines)


async def _dialogue_single(state: AgentState, npc_id: str) -> tuple[str, str, bool]:
    """
    Stage 1: 단일 NPC에 대한 e4b 호출.
    반환: (npc_id, raw_response)
    지식 격리: 각 NPC의 persona/RAG/history를 독립적으로 로드.
    """
    natural_context = state.get("natural_context", "")

    # 동기 파일 I/O — 이벤트 루프 블로킹 방지 위해 스레드 오프로드(VR 실시간 latency).
    persona = await asyncio.to_thread(load_persona, npc_id) or {
        "name": npc_id,
        "importance": "normal",
    }
    persona_name = persona.get("name", npc_id)
    persona_role = persona.get("role", "Inhabitant")
    persona_traits = ", ".join(persona.get("traits", []))

    memory = persona.get("memory_summary", {})
    memory_summary = "; ".join(memory.get("key_events", [])) if memory.get("key_events") else "None"

    vr_context = state.get("vr_context")
    player_id = (
        vr_context.get("player_id", "Player")
        if isinstance(vr_context, dict)
        else getattr(vr_context, "player_id", "Player")
        if vr_context
        else "Player"
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

    rag_context = await asyncio.to_thread(retrieve_context, npc_id, clean_query, 3) if clean_query else ""
    chat_history = get_conversation_context(npc_id, k=5)

    # NPC 인벤토리 — UE5 가 prompt 마다 동적 전송(npc_id → items). 없으면 "None".
    # 주입 목적: NPC 가 보유 아이템만 GiveItem/HandObject 하도록 근거 제공.
    inv_items = []
    if vr_context:
        inv_map = (
            vr_context.get("npc_inventory")
            if isinstance(vr_context, dict)
            else getattr(vr_context, "npc_inventory", None)
        ) or {}
        inv_items = inv_map.get(npc_id, []) or []
    if inv_items:
        inventory_str = ", ".join(
            f"{it.get('name', it.get('id', '?'))}×{it.get('count', 1)}" for it in inv_items
        )
    else:
        inventory_str = "None (empty-handed)"

    fmt_kwargs = dict(
        name=persona_name,
        role=persona_role,
        traits=persona_traits,
        memory=memory_summary,
        sentiment=sentiment,
        rag_context=rag_context if rag_context else "None",
        chat_history=chat_history if chat_history else "No previous conversation",
        inventory=inventory_str,
    )
    # 구조화 호출용(JSON) 프롬프트만 해피패스에서 포맷. 자유텍스트 폴백용
    # system_content 는 except 경로에서만 필요 → lazy(아래 except 에서 포맷).
    structured_content = DIALOGUE_STRUCTURED_PROMPT.format(**fmt_kwargs)

    print(f"[Dialogue] Stage1 e4b: {persona_name} | '{natural_context[:50]}...'")

    raw_response = None
    plan_achieved = False
    try:
        # 구조화 출력: actions 필드를 스키마에 박아 e4b 가 액션을 빠뜨리지 못하게 강제.
        # Ollama format 직접 호출(langchain with_structured_output 우회) — 실측 2.9배 빠름.
        # 획득한 DialogueResponse 를 기존 텍스트 포맷으로 직렬화 → 다운스트림 무변경.
        resp_obj = await ollama_structured(
            structured_content,
            f"Context: {natural_context}",
            DialogueResponse,
            model_name="gemma4_slm",
            temperature=0.7,
            num_predict=300,
        )
        raw_response = _serialize_dialogue(resp_obj).strip()
        plan_achieved = bool(getattr(resp_obj, "plan_achieved", False))
        if plan_achieved:
            print(f"[Dialogue] Stage1 plan 달성 감지 ({npc_id})")
        print(f"[Dialogue] Stage1 응답 ({npc_id}): '{raw_response[:60]}...'")
    except Exception as e:
        print(f"[Dialogue] Stage1 LLM 오류 ({npc_id}): {e}")
        # 폴백 경로에서만 자유텍스트 프롬프트 포맷 (해피패스 낭비 제거).
        system_content = DIALOGUE_SYSTEM_PROMPT.format(**fmt_kwargs)
        cli_prompt = f"{system_content}\n\nContext: {natural_context}\n\nRespond in character now:"
        raw_response = await asyncio.to_thread(call_ollama_direct, cli_prompt, False)
        if raw_response:
            raw_response = _decode_byte_tokens(raw_response.strip())

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

    task = asyncio.create_task(asyncio.to_thread(add_conversation, npc_id, memory_input, speech_for_memory))
    _memory_write_tasks.add(task)
    task.add_done_callback(_on_memory_task_done)

    return npc_id, raw_response, plan_achieved


# [Plan: ...] 추출용 — 12B 출력 형식 두 가지 모두 처리:
#   명세:  [Plan: goal=<목표> | steps=s1;s2;s3]
#   실제:  [Plan: Goal: <목표>; Steps: s1, s2, s3]
# goal/steps 구분자: `|` 또는 `;`, 키워드: `goal=`·`Goal:` / `steps=`·`Steps:`
_PLAN_LINE_RE = re.compile(
    r"\[Plan:\s*(?:goal=|Goal:\s*)(?P<goal>[^\];|]+?)\s*[;|]\s*(?:steps=|Steps:\s*)(?P<steps>[^\]]+)\]",
    re.IGNORECASE,
)


def _parse_plan_line(section_text: str) -> tuple[str, dict | None]:
    """정제된 NPC 섹션에서 [Plan: ...] 라인을 분리.
    반환: (plan 라인 제거된 대사, plan dict 또는 None)."""
    m = _PLAN_LINE_RE.search(section_text)
    if not m:
        return section_text, None
    goal = m.group("goal").strip()
    # 구분자: `;` 또는 `,` 모두 허용 (12B 가 양쪽 모두 사용)
    steps = [s.strip() for s in re.split(r"[;,]", m.group("steps")) if s.strip()]
    cleaned = _PLAN_LINE_RE.sub("", section_text).strip()
    return cleaned, {"goal": goal, "steps": steps}


async def _refine_responses(raw_responses: Dict[str, str], player_id: str) -> tuple[Dict[str, str], Dict[str, dict]]:
    """
    Stage 2: 12B 모델로 전체 NPC 응답 스타일 정제 + plan(goal/steps) 산출.
    사실 추가 금지 — 스타일/유창성 향상만. 재계획(requires_replan=True) 경로에서만 호출.
    단일 12B 호출로 정제와 plan 추출을 동시 수행 (토큰/지연 절약).
    반환: (refined npc_id→대사, npc_plans npc_id→{goal, steps, relation_snapshot}).
    """
    sections = "\n\n".join(f"=== NPC: {npc_id} ===\n{raw}" for npc_id, raw in raw_responses.items())

    print(f"[Dialogue] Stage2 12B 정제+plan 시작 ({len(raw_responses)}개 NPC)")
    refined_text = None
    try:
        llm = get_llm(model_name="gemma4", temperature=0.3, num_predict=500 * len(raw_responses))
        prompt = ChatPromptTemplate.from_messages([("system", REFINE_SYSTEM_PROMPT), ("human", "{sections}")])
        response = await (prompt | llm).ainvoke({"sections": sections})
        refined_text = response.content if hasattr(response, "content") else str(response)
        refined_text = refined_text.strip()
        print(f"[Dialogue] Stage2 정제+plan 완료 ({len(refined_text)} chars)")
    except Exception as e:
        print(f"[Dialogue] Stage2 12B 오류, 원본 유지·plan 생략: {e}")
        return raw_responses, {}

    # 섹션 구분자로 파싱 + plan 라인 분리
    refined: Dict[str, str] = {}
    npc_plans: Dict[str, dict] = {}
    for npc_id in raw_responses:
        pattern = rf"===\s*NPC:\s*{re.escape(npc_id)}\s*===\s*\n?(.*?)(?===\s*NPC:|$)"
        m = re.search(pattern, refined_text, re.DOTALL | re.IGNORECASE)
        section = m.group(1).strip() if m else raw_responses[npc_id]
        if not m:
            print(f"[Dialogue] Stage2 파싱 실패 ({npc_id}), 원본 유지")

        dialogue_text, plan = _parse_plan_line(section)
        refined[npc_id] = dialogue_text
        if plan is not None:
            # relation_snapshot: affinity score만 (확정 결정). 조회 실패 시 0.
            try:
                relation = await db_manager.get_affinity(npc_id, player_id)
                plan["relation_snapshot"] = relation.affinity_score
            except Exception as e:
                print(f"[Dialogue] plan affinity 조회 실패 ({npc_id}): {e}")
                plan["relation_snapshot"] = 0
            npc_plans[npc_id] = plan
        else:
            print(f"[Dialogue] Stage2 plan 라인 누락 ({npc_id})")

    return refined, npc_plans


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

    # 재계획 분기: False=e4b 단독 경량 루프(12B 스킵), True=풀 파이프라인+plan 산출.
    requires_replan = state.get("requires_replan", True)
    print(f"[Dialogue] 대상 NPC: {npcs} | requires_replan={requires_replan}")

    # Stage 1: 병렬 e4b 호출
    results = await asyncio.gather(*[_dialogue_single(state, npc_id) for npc_id in npcs])
    raw_responses: Dict[str, str] = {r[0]: r[1] for r in results}
    plan_achieved_map: Dict[str, bool] = {r[0]: r[2] for r in results}

    # Stage 2: 12B 정제+plan — 재계획 시에만. 경량 루프는 e4b 단독으로 종료.
    npc_plans: Dict[str, dict] = {}
    if requires_replan:
        vr_context = state.get("vr_context")
        player_id = (
            vr_context.get("player_id", "Player")
            if isinstance(vr_context, dict)
            else getattr(vr_context, "player_id", "Player")
            if vr_context
            else "Player"
        )
        raw_responses, npc_plans = await _refine_responses(raw_responses, player_id)
    else:
        print("[Dialogue] 경량 루프: Stage2 12B 스킵 (e4b 단독)")

    # 단일 NPC 호환: raw_response 도 채움
    single_npc = npcs[0]
    raw_response = raw_responses.get(single_npc, "")

    return {
        "raw_responses": raw_responses,
        "raw_response": raw_response,
        "npc_plans": npc_plans or None,
        "plan_achieved": plan_achieved_map or None,
        "target_npc": single_npc,
        "current_speaker": "Dialogue",
        "next": "Interface_Output",
    }
