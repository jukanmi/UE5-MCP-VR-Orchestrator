"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: dialogue.py                                                           ║
║ Role: CREATIVE RESPONSE GENERATOR (Multi-NPC 3-Stage Pipeline)             ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Generate natural, in-character NPC responses based on persona, context,   ║
║   and conversational history. Stage1 은 grammar 강제 DialogueResponse 를    ║
║   산출 → structured_responses 로 직접 전달 (Stage3 정규식 재파싱 없음).      ║
║   raw_responses(텍스트)는 Stage2 plan·메모리·단일호환용으로 병행 직렬화.     ║
║                                                                              ║
║ PIPELINE (3-Stage):                                                          ║
║   Stage 1: e4b × N 병렬 — NPC별 독립 호출 (지식 오염 없음, 대사 최종본)   ║
║   Stage 2: 12B × 1 plan 산출 — 재계획 시만, 대사 무변경 (구조화 JSON)     ║
║   Stage 3: interface_output.py 에서 structured_responses→ActionBatch 변환   ║
║                                                                              ║
║ INPUT:  target_npcs (List[str]), natural_context (str)                      ║
║ OUTPUT: structured_responses (Dict[str, DialogueResponse]) + raw_responses  ║
║                                                                              ║
║ OUTPUT FORMAT (CRITICAL):                                                   ║
║   DialogueResponse JSON (mode/facial/speech/tone/actions/plan_achieved)     ║
║   — grammar 강제. 텍스트 직렬화는 Stage2 plan 입력·단일호환 파생 렌더링뿐.  ║
║                                                                              ║
║ LLM SELECTION:                                                               ║
║   Stage 1 — e4b (경량, 병렬 VRAM 효율)                                     ║
║   Stage 2 — gemma4-12b (plan 산출, 단일 구조화 호출)                       ║
╚══════════════════════════════════════════════════════════════════════════════╝
"""

import yaml
import os
import re
import asyncio
from dataclasses import dataclass
from functools import lru_cache
from typing import Dict
from ...utils.llm_factory import ollama_structured
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from ..state import AgentState
from ...utils import db_manager
from ...utils.id_utils import ci_id_map
from ...schemas.actions import DialogueResponse, PlanBatchResponse, DIALOGUE_ACTION_FIELD_MAP


PERSONAS_BASE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "personas")


def _dialogue_schema_with_targets(valid_targets) -> dict | None:
    """valid_targets 있으면 DialogueActionItem.target 에 enum 을 주입한 스키마 반환 —
    Ollama grammar 수준에서 해석 불가 자유문자열 target("Strategic Position" 류) 원천 차단.
    없으면 None(ollama_structured 가 기본 model_json_schema 사용)."""
    if not valid_targets:
        return None
    return _schema_with_target_enum(tuple(valid_targets))


@lru_cache(maxsize=8)
def _schema_with_target_enum(valid_targets: tuple) -> dict | None:
    """target enum 주입 스키마 캐시 — model_json_schema() 는 pydantic 필드 그래프 전체를
    재순회하므로 NPC×턴마다 재생성하지 않는다(동일 타깃 목록이면 동일 dict 재사용).
    반환 dict 는 호출측에서 수정 금지(공유 캐시)."""
    schema = DialogueResponse.model_json_schema()
    target = schema.get("$defs", {}).get("DialogueActionItem", {}).get("properties", {}).get("target")
    if target is None:
        # pydantic 스키마 구조 변경 시 조용한 미적용 방지 — 로그 남기고 기본 스키마.
        print("[Dialogue] target enum 주입 실패: 스키마에 DialogueActionItem.target 없음")
        return None
    # "" = target 미사용 액션(스키마 default). description 의 자유서술과 enum 충돌 방지 위해 교체.
    target["enum"] = [""] + list(valid_targets)
    target.pop("description", None)
    return schema


_memory_write_tasks: set = set()


def _on_memory_task_done(task) -> None:
    _memory_write_tasks.discard(task)
    if not task.cancelled() and task.exception():
        print(f"[Dialogue] 메모리 기록 백그라운드 실패: {task.exception()}")


# 구조화 출력(with_structured_output) 전용 프롬프트. 텍스트 태그 포맷 대신 JSON 필드
# (speech/actions)를 채우도록 지시. 해피패스·폴백(target enum 없는 재시도) 모두 이 프롬프트로
# ollama_structured 호출 → 전 경로가 DialogueResponse 산출(자유텍스트 파싱층 제거).
DIALOGUE_STRUCTURED_PROMPT = """You are {name}, a {role}, an NPC in a VR game.
Personality traits: {traits}
말투 (이렇게 말한다):
{speech_style}

Respond ONLY as a JSON object with fields: mode, facial, speech, tone, actions, plan_achieved.
- facial: MUST match the situation RIGHT NOW. Threat/combat → Angry or Fear.
  Sudden bad news → Surprised. Calm chat → Neutral. NEVER default to Tired unless exhausted.
- speech: your spoken line, in character, 1-3 sentences in Korean (NEVER empty, ALWAYS Korean).
  자연스러운 한국어 구어로만. 실제 사람이 말하듯 구체적·일상적으로. 뜻이 통하지 않는 추상적 은유·거창한 비유·공허한 미사여구 금지.
  나쁜 예(금지): "내 그림자조차 네 눈에 다 비치게 해 줄 테니." / 좋은 예: "허튼짓 마라, 다 지켜보고 있으니까."
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
Use ONLY a type from this list. target MUST be one of: {valid_targets}. Never invent other target names.

YOUR inventory (items you currently hold): {inventory}
 - Only GiveItem/HandObject/UseItem/Equip an item that is in YOUR inventory above.
 - If asked for an item you do NOT have, say so — do NOT emit a give/use action for it.

Recent memory: {memory}
Current sentiment toward player: {sentiment}
Relevant context: {rag_context}
Conversation history: {chat_history}"""

# Stage2 는 plan 산출 전용. 과거 '정제+plan 동시 출력'(REFINE_SYSTEM_PROMPT)은
# 12B(abliterated)가 정제 지시를 무시하고 [Plan:] 라인만 출력 → _parse_plan_line 이
# plan 을 떼면 대사가 빈 문자열로 증발하는 문제 실측(2026-07, 재현 4/4).
# few-shot·rewrite 강제·구조화 출력 모두 정제 거부(8/8) — 정제는 Stage1 e4b 프롬프트의
# anti-미사여구 규칙이 이미 담당하므로 Stage2 역할을 plan 으로 축소, grammar 강제로 형식 보장.
PLAN_SYSTEM_PROMPT = """You are a forward planner for NPC dialogue in a VR game.
For EACH === NPC: <id> === section in the input, produce one plan entry.

- npc_id: copy EXACTLY from the section header.
- goal: what this NPC concretely pursues over the next few turns. 짧은 한국어 구절.
  구체적 결과를 써라 — "전투 목표 및 계획" 같은 뭉뚱그린 표현 금지.
  좋은 예: "동쪽 다리에서 좀비 침입 저지"
- steps: 2-4 concrete ordered beats, 한국어. 각 step 은 NPC 가 실제로 할 행동/대화 비트.
  번호 접두사 없이. 좋은 예: ["동쪽 다리로 이동", "아군에게 신호", "교전 개시"]

Base the plan ONLY on facts already present in each NPC's section — do NOT invent new lore.

Input format:
=== NPC: <id> ===
<npc response with [Mode:]/[Action:] tags>"""


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


def _format_speech_style(raw) -> str:
    """persona speech_style(list|str|None) → 프롬프트 주입용 불릿 문자열.
    list 면 각 원소를 '- ' 불릿 개행 결합, str 이면 그대로, 비면 기본값. (list/str 하위호환)"""
    default = "- 자연스러운 일상 구어체로 말한다."
    if not raw:
        return default
    if isinstance(raw, str):
        return raw
    lines = [f"- {str(s).strip()}" for s in raw if str(s).strip()]
    return "\n".join(lines) if lines else default


def _create_persona(agent_id: str) -> dict:
    persona = {
        "name": agent_id,
        "importance": "normal",
        "role": "Inhabitant",
        "traits": ["Cautious", "Reserved", "Observant"],
        "speech_style": ["자연스러운 일상 구어체로 말한다."],
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
    """구조화 DialogueResponse → 자유텍스트 포맷 직렬화.
    Stage2 12B plan 입력 섹션 + 단일 NPC 호환용 텍스트를 [Mode:][Facial:]
    "speech"[Action:] 형태로 재생 (Stage3 는 structured_responses 를 직접 소비).
    speech/tone 은 _run_stage1_llm 이 이미 _decode_byte_tokens 처리 — 재디코드 안 함."""
    lines = [f"[Mode: {obj.mode}] [Facial: {obj.facial}]"]

    speech = (obj.speech or "").strip()
    if speech:
        tone = (obj.tone or "").strip()
        lines.append(f'"{speech}" ({tone})' if tone else f'"{speech}"')

    # 태그 키 = DialogueActionItem 필드명 — DIALOGUE_ACTION_FIELD_MAP 단일 소스.
    for act in obj.actions:
        parts = [act.type]
        for _, field_name in DIALOGUE_ACTION_FIELD_MAP:
            v = (getattr(act, field_name) or "").strip()
            if v:
                parts.append(f"{field_name}={v}")
        lines.append(f"[Action: {' '.join(parts)}]")

    return "\n".join(lines)


def _vr_get(vr_context, key: str, default=None):
    """vr_context(dict 또는 GesPrompt 객체)에서 key 추출 — 없으면 default.
    interface 경계에서 dict/pydantic 둘 다 흘러들어와 매 필드마다 반복되던
    isinstance 분기를 단일화."""
    if not vr_context:
        return default
    if isinstance(vr_context, dict):
        return vr_context.get(key, default)
    return getattr(vr_context, key, default)


def _vr_player_id(vr_context) -> str:
    """vr_context 에서 player_id 추출 — 없으면 "Player"."""
    return _vr_get(vr_context, "player_id", "Player") or "Player"


@dataclass
class _Stage1Context:
    """_dialogue_single Stage1 입력 번들 — 컨텍스트 수집 단계와 LLM 호출 단계의 경계."""

    fmt_kwargs: dict  # 프롬프트 포맷 인자 (structured/freetext 공용)
    valid_targets: list | None  # target enum grammar 강제용 (없으면 None)
    clean_query: str  # 플레이어 발화 (RAG·메모리 입력)
    natural_context: str  # supervisor 조립 컨텍스트 (LLM user 메시지)


async def _collect_stage1_context(state: AgentState, npc_id: str) -> _Stage1Context:
    """persona/affinity/RAG/history/inventory/valid_targets 를 독립 로드해 프롬프트 인자 조립.
    지식 격리: 각 NPC 의 컨텍스트를 다른 NPC 와 섞지 않고 독립적으로 수집."""
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
    player_id = _vr_player_id(vr_context)

    try:
        relation = await db_manager.get_affinity(npc_id, player_id)
        sentiment = f"{relation.reputation_tag} (Score: {relation.affinity_score})"
    except Exception as e:
        print(f"[Dialogue] Affinity 조회 실패 ({npc_id}): {e}")
        sentiment = memory.get("sentiment", "Neutral")

    clean_query = _vr_get(vr_context, "voice_transcript", "") or ""

    rag_context = await asyncio.to_thread(retrieve_context, npc_id, clean_query, 3) if clean_query else ""
    chat_history = get_conversation_context(npc_id, k=5)

    # NPC 인벤토리 — UE5 가 prompt 마다 동적 전송(npc_id → items). 없으면 "None".
    # 주입 목적: NPC 가 보유 아이템만 GiveItem/HandObject 하도록 근거 제공.
    inv_map = _vr_get(vr_context, "npc_inventory", None) or {}
    inv_items = inv_map.get(npc_id, []) or []
    if inv_items:
        inventory_str = ", ".join(f"{it.get('name', it.get('id', '?'))}×{it.get('count', 1)}" for it in inv_items)
    else:
        inventory_str = "None (empty-handed)"

    # 유효 타깃 vocabulary — UE5 ResolveActionTarget 해석 가능 키워드(valid_targets).
    # 있으면 프롬프트 명시 + 스키마 target enum 강제, 없으면 종전 자유문자열(하위호환).
    valid_targets = _vr_get(vr_context, "valid_targets", None)
    valid_targets_str = ", ".join(valid_targets) if valid_targets else "Player, Self, Enemy, or an NPC name"

    fmt_kwargs = dict(
        name=persona_name,
        role=persona_role,
        traits=persona_traits,
        speech_style=_format_speech_style(persona.get("speech_style")),
        memory=memory_summary,
        sentiment=sentiment,
        rag_context=rag_context if rag_context else "None",
        chat_history=chat_history if chat_history else "No previous conversation",
        inventory=inventory_str,
        valid_targets=valid_targets_str,
    )
    return _Stage1Context(fmt_kwargs, valid_targets, clean_query, natural_context)


async def _run_stage1_llm(ctx: _Stage1Context, npc_id: str) -> tuple[DialogueResponse, bool]:
    """Stage1 e4b 호출 — 구조화 해피패스 → 구조화 폴백(target enum 없음) → 기본 응답.
    전 경로가 DialogueResponse 산출(자유텍스트 파싱층 제거). 반환: (resp, plan_achieved)."""
    structured_content = DIALOGUE_STRUCTURED_PROMPT.format(**ctx.fmt_kwargs)
    user_content = f"Context: {ctx.natural_context}"

    print(f"[Dialogue] Stage1 e4b: {ctx.fmt_kwargs['name']} | '{ctx.natural_context[:50]}...'")

    resp = None
    try:
        # 구조화 출력: actions 필드를 스키마에 박아 e4b 가 액션을 빠뜨리지 못하게 강제.
        # Ollama format 직접 호출(langchain with_structured_output 우회) — 실측 2.9배 빠름.
        resp = await ollama_structured(
            structured_content,
            user_content,
            DialogueResponse,
            # valid_targets 있으면 target enum grammar 강제 (없으면 None → 기본 스키마).
            schema_override=_dialogue_schema_with_targets(ctx.valid_targets),
            model_name="gemma4_slm",
            # temp 0.7→0.5: 미사여구 드리프트 억제(Tier2). 0.4 는 반복적, 0.6 는 과격/장황 드리프트 —
            # 스윕 결과 0.5 가 자연스러움·다양성·캐릭터 유지 균형점(실측).
            temperature=0.5,
            num_predict=300,
        )
    except Exception as e:
        print(f"[Dialogue] Stage1 LLM 오류 ({npc_id}), 구조화 폴백 재시도: {e}")
        # 폴백도 구조화: target enum 없이(폴백은 valid_targets 미보장) 기본 스키마 + temp↑(다양성).
        try:
            resp = await ollama_structured(
                structured_content,
                user_content,
                DialogueResponse,
                model_name="gemma4_slm",
                temperature=0.7,
                num_predict=300,
            )
        except Exception as e2:
            print(f"[Dialogue] Stage1 폴백 재시도 실패 ({npc_id}), 기본 응답: {e2}")
            resp = None

    if resp is None:
        resp = DialogueResponse(mode="Social", facial="Neutral", speech="...", tone="confused", actions=[])

    # byte-fallback 토큰 정제 — 구조화 obj 필드에 in-place(정규식 텍스트 왕복 없이 parity).
    # cross-module 정제 중복 회피: interface_output 은 이미 정제된 speech 가정.
    resp.speech = _decode_byte_tokens((resp.speech or "").strip()) or "..."
    resp.tone = _decode_byte_tokens((resp.tone or "").strip())

    plan_achieved = bool(resp.plan_achieved)
    if plan_achieved:
        print(f"[Dialogue] Stage1 plan 달성 감지 ({npc_id})")
    print(f"[Dialogue] Stage1 응답 ({npc_id}): '{resp.speech[:60]}...'")
    return resp, plan_achieved


def _record_dialogue_memory(npc_id: str, resp: DialogueResponse, clean_query: str, natural_context: str) -> None:
    """resp.speech 를 fire-and-forget 메모리 기록 (구조화 obj 직접 사용, 정규식 추출 제거)."""
    speech_for_memory = (resp.speech or "").strip()[:100]
    memory_input = clean_query if clean_query else natural_context

    task = asyncio.create_task(asyncio.to_thread(add_conversation, npc_id, memory_input, speech_for_memory))
    _memory_write_tasks.add(task)
    task.add_done_callback(_on_memory_task_done)


async def _dialogue_single(state: AgentState, npc_id: str) -> tuple[str, DialogueResponse, bool]:
    """
    Stage 1: 단일 NPC에 대한 e4b 호출. 반환: (npc_id, resp, plan_achieved).
    수집(_collect_stage1_context) → 생성(_run_stage1_llm) → 기록(_record_dialogue_memory).
    """
    ctx = await _collect_stage1_context(state, npc_id)
    resp, plan_achieved = await _run_stage1_llm(ctx, npc_id)
    _record_dialogue_memory(npc_id, resp, ctx.clean_query, ctx.natural_context)
    return npc_id, resp, plan_achieved


async def _generate_plans(raw_responses: Dict[str, str], player_id: str) -> Dict[str, dict]:
    """
    Stage 2: 12B plan 전용 산출. 재계획(requires_replan=True) 경로에서만 호출.
    대사는 건드리지 않음 — Stage1 출력이 그대로 최종 (정제는 Stage1 프롬프트가 담당).
    grammar 강제(PlanBatchResponse)로 goal/steps 형식 보장 — 텍스트 [Plan:] 파싱 제거.
    반환: npc_plans npc_id→{goal, steps, relation_snapshot}. 실패 시 {} (plan 생략).
    """
    sections = "\n\n".join(f"=== NPC: {npc_id} ===\n{raw}" for npc_id, raw in raw_responses.items())

    print(f"[Dialogue] Stage2 12B plan 산출 시작 ({len(raw_responses)}개 NPC)")
    try:
        result = await ollama_structured(
            PLAN_SYSTEM_PROMPT,
            sections,
            PlanBatchResponse,
            model_name="gemma4",
            temperature=0.3,
            # plan-only 는 NPC 당 ~100토큰 (goal 1구절 + steps 2-4개). 여유 2배.
            num_predict=220 * len(raw_responses),
        )
    except Exception as e:
        print(f"[Dialogue] Stage2 12B 오류, plan 생략: {e}")
        return {}

    # npc_id 매칭: 12B 가 헤더를 그대로 복사하지만 대소문자 흔들림 대비 lower 매핑.
    id_map = ci_id_map(raw_responses)
    npc_plans: Dict[str, dict] = {}
    for item in result.npcs:
        npc_id = id_map.get(item.npc_id.strip().lower())
        if npc_id is None:
            print(f"[Dialogue] Stage2 미상 npc_id 무시: '{item.npc_id}'")
            continue
        # 12B 가 간혹 "1. " 번호 접두사를 붙임 — 제거 (실측).
        steps = [re.sub(r"^\s*\d+[.)]\s*", "", s).strip() for s in item.steps if s.strip()]
        plan = {"goal": item.goal.strip(), "steps": steps}
        # relation_snapshot: affinity score만 (확정 결정). 조회 실패 시 0.
        try:
            relation = await db_manager.get_affinity(npc_id, player_id)
            plan["relation_snapshot"] = relation.affinity_score if relation else 0
        except Exception as e:
            print(f"[Dialogue] plan affinity 조회 실패 ({npc_id}): {e}")
            plan["relation_snapshot"] = 0
        npc_plans[npc_id] = plan

    for npc_id in raw_responses:
        if npc_id not in npc_plans:
            print(f"[Dialogue] Stage2 plan 누락 ({npc_id})")
    print(f"[Dialogue] Stage2 plan 산출 완료 ({len(npc_plans)}/{len(raw_responses)}개)")
    return npc_plans


async def dialogue_node(state: AgentState):
    """
    Dialogue Agent (3-Stage Multi-NPC).

    Stage 1: e4b × N 병렬 (지식 격리, 각 NPC 독립 호출) — 대사 최종본
    Stage 2: 12B × 1 plan 산출 (requires_replan=True 시만, 대사 무변경)
    Output:  raw_responses Dict[npc_id, str]
    """
    npcs = state.get("target_npcs") or []
    if not npcs:
        single = state.get("target_npc", "Elara")
        npcs = [single] if single else ["Elara"]

    # 재계획 분기: False=e4b 단독 경량 루프(12B 스킵), True=풀 파이프라인+plan 산출.
    requires_replan = state.get("requires_replan", True)
    print(f"[Dialogue] 대상 NPC: {npcs} | requires_replan={requires_replan}")

    # Stage 1: 병렬 e4b 호출 — 구조화 DialogueResponse 직접 산출.
    results = await asyncio.gather(*[_dialogue_single(state, npc_id) for npc_id in npcs])
    structured_responses: Dict[str, DialogueResponse] = {r[0]: r[1] for r in results}
    plan_achieved_map: Dict[str, bool] = {r[0]: r[2] for r in results}
    single_npc = npcs[0]

    # Stage 2: 12B plan 산출 — 재계획 시에만. 경량 루프는 e4b 단독으로 종료.
    # 대사는 Stage1 출력 그대로 (12B 정제 제거 — PLAN_SYSTEM_PROMPT 상단 주석 참조).
    # raw_responses(텍스트)는 소비처 기준 필요 시점에만 직렬화:
    #   replan 턴 = Stage2 plan 입력(전 NPC) / 경량 루프 = 단일 NPC 호환 1건만 (N-1 낭비 제거).
    npc_plans: Dict[str, dict] = {}
    if requires_replan:
        raw_responses: Dict[str, str] = {
            npc_id: _serialize_dialogue(resp) for npc_id, resp in structured_responses.items()
        }
        vr_context = state.get("vr_context")
        player_id = _vr_player_id(vr_context)
        npc_plans = await _generate_plans(raw_responses, player_id)
    else:
        print("[Dialogue] 경량 루프: Stage2 12B 스킵 (e4b 단독)")
        raw_responses = {single_npc: _serialize_dialogue(structured_responses[single_npc])}

    # 단일 NPC 호환: raw_response 도 채움
    raw_response = raw_responses.get(single_npc, "")

    return {
        "structured_responses": structured_responses,
        "raw_responses": raw_responses,
        "raw_response": raw_response,
        "npc_plans": npc_plans or None,
        "plan_achieved": plan_achieved_map or None,
        "target_npc": single_npc,
        "current_speaker": "Dialogue",
        "next": "Interface_Output",
    }
