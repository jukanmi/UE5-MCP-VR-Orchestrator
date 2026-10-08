"""
╔══════════════════════════════════════════════════════════════════════════════╗
║ File: dialogue.py                                                           ║
║ Role: CREATIVE RESPONSE GENERATOR (Multi-NPC 3-Stage Pipeline)             ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ CORE RESPONSIBILITY (UNCHANGING):                                           ║
║   Generate natural, in-character NPC responses based on persona, context,   ║
║   and conversational history. Stage1 은 grammar 강제 DialogueResponse 를    ║
║   산출 → structured_responses 로 직접 전달 (Stage3 정규식 재파싱 없음).      ║
║   raw_responses(텍스트)는 replan 턴의 Stage2 plan 입력용으로만 직렬화.       ║
║                                                                              ║
║ PIPELINE (3-Stage):                                                          ║
║   Stage 1: e4b × N 병렬 — NPC별 독립 호출 (지식 오염 없음, 대사 최종본)   ║
║   Stage 2: 플래너 × 1 plan 산출 — 재계획 시만, 대사 무변경 (구조화 JSON)  ║
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

import logging
import yaml
import os
import re
import asyncio
import random
from difflib import SequenceMatcher
from dataclasses import dataclass
from functools import lru_cache
from typing import Dict, Optional
from ...utils.llm_factory import ollama_structured, stage2_structured, STAGE1_MODEL
from ...utils.rag_utils import retrieve_context
from ...utils.memory_manager import get_conversation_context, add_conversation
from ...utils.async_tasks import spawn_background
from ..party_intent import detect_party_intent
from ..state import AgentState
from ...schemas.vr_context import GesPrompt
from ...utils import db_manager
from ...utils.id_utils import ci_get, ci_id_map
from ...schemas.actions import DEFAULT_NPC, DialogueResponse, PlanBatchResponse, DIALOGUE_ACTION_FIELD_MAP
from ...server_state import STATE
from .prompts import DIALOGUE_STRUCTURED_PROMPT, PARTY_PROMPT_LINE, PARTY_VOCAB_PROMPT, PLAN_SYSTEM_PROMPT


logger = logging.getLogger(__name__)

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
        logger.error("[Dialogue] target enum 주입 실패: 스키마에 DialogueActionItem.target 없음")
        return None
    # "" = target 미사용 액션(스키마 default). description 의 자유서술과 enum 충돌 방지 위해 교체.
    target["enum"] = [""] + list(valid_targets)
    target.pop("description", None)
    return schema


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
        logger.warning(f"[Dialogue] Persona '{agent_id}' not found → created: {save_path}")
    except Exception as e:
        logger.error(f"[Dialogue] Persona 파일 생성 실패: {e}")
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
    Stage2 plan 입력 섹션 + 단일 NPC 호환용 텍스트를 [Mode:][Facial:]
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

    # 스토리 디렉터 goal — Stage2(plan)만 받던 걸 대사에도 1줄. Stage1 출력이 그대로 최종 대사라
    # 비트 첫 턴이 목표를 모르고 말하던 문제(Guard 가 피난처 안내를 안 함). 없으면 "None".
    directive = ci_get(state.get("story_directive") or {}, npc_id) or {}
    story_goal = directive.get("goal") or "None"

    vr_context: GesPrompt = state["vr_context"]  # interface_input 이 항상 객체로 정규화
    player_id = vr_context.player_id or "Player"

    try:
        relation = await db_manager.get_affinity(npc_id, player_id)
        sentiment = f"{relation.reputation_tag} (Score: {relation.affinity_score})"
    except Exception as e:
        logger.error(f"[Dialogue] Affinity 조회 실패 ({npc_id}): {e}")
        sentiment = memory.get("sentiment", "Neutral")

    clean_query = vr_context.voice_transcript or ""

    rag_context = await asyncio.to_thread(retrieve_context, npc_id, clean_query, 3) if clean_query else ""
    # 캐시 미스 시 ConversationMemory._load_from_file 이 동기 JSON read 를 수행한다.
    # 위 load_persona/retrieve_context 와 마찬가지로 오프로드하지 않으면 이벤트 루프가
    # 멈춰 동시 처리 중인 다른 NPC 까지 전부 지연된다.
    chat_history = await asyncio.to_thread(get_conversation_context, npc_id, 5)

    # NPC 인벤토리 — UE5 가 prompt 마다 동적 전송(npc_id → items). 없으면 "None".
    # 주입 목적: NPC 가 보유 아이템만 GiveItem/HandObject 하도록 근거 제공.
    inv_map = vr_context.npc_inventory or {}
    inv_items = inv_map.get(npc_id, []) or []
    if inv_items:
        # 표시명과 함께 id 를 괄호로 노출한다. GiveItem/UseItem 은 UE5 에서 ItemID 로 조회되는데,
        # 표시명만 주면(예: "돌멩이") LLM 이 그걸 그대로 give_item_id 에 넣어 조회가 0 건이 되고
        # "아이템 없음" 으로 죽는다 — 표시명↔id 매핑 근거가 프롬프트에 있어야 한다.
        inventory_str = ", ".join(
            f"{it.get('name', '?')}({it.get('id', '?')})×{it.get('count', 1)}" for it in inv_items
        )
    else:
        inventory_str = "None (empty-handed)"

    # 유효 타깃 vocabulary — UE5 ResolveActionTarget 해석 가능 키워드(valid_targets).
    # 있으면 프롬프트 명시 + 스키마 target enum 강제, 없으면 종전 자유문자열(하위호환).
    valid_targets = vr_context.valid_targets
    valid_targets_str = ", ".join(valid_targets) if valid_targets else "Player, Self, Enemy, or an NPC name"

    fmt_kwargs = dict(
        name=persona_name,
        role=persona_role,
        traits=persona_traits,
        speech_style=_format_speech_style(persona.get("speech_style")),
        memory=memory_summary,
        sentiment=sentiment,
        story_goal=story_goal,
        rag_context=rag_context if rag_context else "None",
        chat_history=chat_history if chat_history else "No previous conversation",
        inventory=inventory_str,
        valid_targets=valid_targets_str,
    )
    return _Stage1Context(fmt_kwargs, valid_targets, clean_query, natural_context)


# 파티 합류 게이트 — NPC→플레이어 호감도가 이 값 이상일 때만 JoinParty 허용(초안값, PIE 에서 확정).
PARTY_JOIN_AFFINITY = 20
# C++ UPartySubsystem::MaxSize 와 같은 값. C++ 가드가 최종 방어이고 서버는 거절 대사를 위해 미리 안다.
PARTY_MAX_SIZE = 4

# LLM 재호출 없이 결정론적으로 쓰는 거절 대사(합류 수락 대사를 덮어쓴다).
_PARTY_REFUSE_LOW_AFFINITY = (
    "미안하지만 아직은 같이 갈 만큼 서로를 잘 모르잖아요.",
    "글쎄요, 지금은 같이 다니기가 좀 어렵겠어요.",
    "선뜻 따라나서기엔 아직 믿음이 부족해요. 미안해요.",
)
_PARTY_REFUSE_FULL = (
    "이미 일행이 가득 찼어요. 지금은 더 같이 갈 수가 없겠네요.",
    "함께 다니는 사람이 너무 많아요. 다음에 기회가 되면 같이 가요.",
)


async def _gate_party_actions(resp: DialogueResponse, npc_id: str, player_id: str) -> None:
    """Stage1 응답의 JoinParty/LeaveParty 를 서버 상태로 사후검증(in-place, LLM 재호출 없음).
    - JoinParty: 이미 멤버면 제거(무의미). 비멤버인데 정원 초과 또는 호감도 미달이면 제거하고 speech 를 거절 대사로 교체
      (수락 대사가 남으면 NPC 가 승낙하고도 안 따라오는 모순이 된다).
    - LeaveParty: 멤버가 아니면 제거."""
    if not any(a.type in ("JoinParty", "LeaveParty") for a in resp.actions):
        return

    is_member = npc_id in STATE.party_members
    refuse: tuple[str, ...] | None = None
    kept = []
    for act in resp.actions:
        if act.type == "LeaveParty" and not is_member:
            logger.info(f"[Party] 비멤버 LeaveParty 제거: {npc_id}")
            continue
        if act.type == "JoinParty":
            if is_member:
                logger.info(f"[Party] 이미 멤버인 JoinParty 제거: {npc_id}")
                continue
            if len(STATE.party_members) >= PARTY_MAX_SIZE:
                refuse = _PARTY_REFUSE_FULL
            else:
                try:
                    score = (await db_manager.get_affinity(npc_id, player_id)).affinity_score
                except Exception as e:
                    logger.error(f"[Party] 호감도 조회 실패 ({npc_id}), 거절 처리: {e}")
                    score = 0
                if score < PARTY_JOIN_AFFINITY:
                    refuse = _PARTY_REFUSE_LOW_AFFINITY
                    logger.info(f"[Party] 호감도 미달 JoinParty 거절: {npc_id} ({score} < {PARTY_JOIN_AFFINITY})")
            if refuse:
                continue
        kept.append(act)
    if refuse:
        # 거절했는데 플레이어를 따라가면 "못 가요" 하면서 따라오는 모순 — 플레이어 대상(빈 타깃 포함) Follow/Track 도 뺀다.
        player_keys = {"", "player", player_id.lower()}
        kept = [a for a in kept if not (a.type in ("Follow", "Track") and a.target.lower() in player_keys)]
        # 수락 표정·어조가 거절 대사에 붙지 않게 중립으로 되돌린다.
        resp.speech = random.choice(refuse)
        resp.facial = "Neutral"
        resp.tone = ""
    resp.actions = kept


def _apply_party_intent(resp: DialogueResponse, npc_id: str, transcript: str, player_id: str) -> None:
    """SLM 이 못 내는 파티 어휘를 플레이어 발화 키워드로 보정(in-place). _gate_party_actions 앞에서 호출 —
    치환된 JoinParty 가 호감도·정원 게이트를 그대로 거친다. 액션을 새로 만들지는 않고 치환만 한다.
    - 초대 + 비멤버: 플레이어 대상 Follow → JoinParty ("같이 가자" 가 학습상 Follow 라서).
    - 해산 + 멤버: Follow(플레이어 대상)/Stop → LeaveParty.
    - 멤버의 Stop: 해산 키워드가 없으면 Idle ("멈춰" 한마디로 UE 에서 Stop=해산 되는 것 방지)."""
    intent = detect_party_intent(transcript)
    is_member = npc_id in STATE.party_members
    player_keys = {"", "player", player_id.lower()}

    out = []
    for act in resp.actions:
        player_follow = act.type == "Follow" and act.target.lower() in player_keys
        if intent == "invite" and not is_member and player_follow:
            logger.info(f"[Party] 초대 발화 Follow → JoinParty 치환: {npc_id}")
            act.type, act.target = "JoinParty", ""
        elif intent == "dismiss" and is_member and (player_follow or act.type == "Stop"):
            logger.info(f"[Party] 해산 발화 {act.type} → LeaveParty 치환: {npc_id}")
            act.type, act.target = "LeaveParty", ""
        elif is_member and act.type == "Stop":
            logger.info(f"[Party] 해산 키워드 없는 파티원 Stop → Idle 치환: {npc_id}")
            act.type = "Idle"
        if act.type in ("JoinParty", "LeaveParty") and any(o.type == act.type for o in out):
            continue  # 치환 결과가 SLM 이 이미 낸 같은 액션과 겹치면 하나만
        out.append(act)
    resp.actions = out


# 직전 발화 복사 판정 — 공백·구두점 제거 후 유사도(SequenceMatcher ratio)가 이 값 이상이면 반복으로 본다.
REPEAT_SIMILARITY = 0.85
# 정규화 후 이 길이 미만(예: "네", "그래요")은 반복이 자연스러우므로 검사하지 않는다.
REPEAT_MIN_CHARS = 4
# 반복 시 1회 재생성 온도 (기본 0.5).
REPEAT_RETRY_TEMPERATURE = 0.8


def _norm_speech(text: str) -> str:
    return re.sub(r"[\W_]+", "", text)


# 문장 단위 반복 판정 — 문장 하나를 매번 그대로 붙이면 전체 유사도는 낮아도(실측 0.765) 복사 대사다.
# 정규화 길이가 이 값 미만인 문장("안녕", "알겠어")은 겹쳐도 자연스러우므로 제외.
REPEAT_SENTENCE_MIN_CHARS = 8
REPEAT_SENTENCE_SIMILARITY = 0.9


def _sentences(text: str) -> list[str]:
    """., ?, !, 。, 줄바꿈 기준 문장 분리(빈 조각 제거)."""
    return [t.strip() for t in re.split(r"[.?!。\n]+", text) if t.strip()]


def _repeated_line(speech: str, chat_history: str, npc_id: str) -> str:
    """기록(chat_history 문자열)의 같은 NPC 발화와 겹치는 부분을 돌려준다. 없으면 "".
    1) 전체 발화가 직전 줄과 동일/매우 유사하면 그 줄 전체, 2) 아니면 긴 문장 하나라도 이전 발화의
    문장과 (거의) 같으면 그 문장. 재생성 금지 문구에 이 반환값을 쓴다."""
    new = _norm_speech(speech)
    prefix = f"{npc_id}: ".casefold()
    prevs = [ln[len(prefix):].strip() for ln in chat_history.splitlines() if ln.casefold().startswith(prefix)]

    if len(new) >= REPEAT_MIN_CHARS:
        for prev in prevs:
            old = _norm_speech(prev)
            if old and (old == new or SequenceMatcher(None, old, new).ratio() >= REPEAT_SIMILARITY):
                return prev

    old_sents = _history_sentences(prevs)
    for sent in _sentences(speech):
        if _sentence_repeated(sent, old_sents):
            return sent
    return ""


def _history_sentences(prevs: list[str]) -> list[str]:
    """이전 발화들의 문장을 정규화한 목록(길이 기준 미달 제외)."""
    return [n for prev in prevs for sent in _sentences(prev) if len(n := _norm_speech(sent)) >= REPEAT_SENTENCE_MIN_CHARS]


def _sentence_repeated(sent: str, old_norms: list[str]) -> bool:
    ns = _norm_speech(sent)
    if len(ns) < REPEAT_SENTENCE_MIN_CHARS:
        return False
    return any(old == ns or SequenceMatcher(None, old, ns).ratio() >= REPEAT_SENTENCE_SIMILARITY for old in old_norms)


# 반복 문장을 뺀 뒤 남은 대사의 정규화 길이가 이 값 미만이면 빼지 않는다(빈 대사 방지).
REPEAT_STRIP_MIN_CHARS = 4


def _strip_repeated_sentences(speech: str, chat_history: str, npc_id: str) -> tuple[str, list[str]]:
    """이전 발화와 겹치는 문장만 speech 에서 제거한다. 반환: (남은 대사, 제거한 문장들).
    제거 후 남는 게 너무 짧거나 제거할 게 없으면 (원본, []) — 재생성도 금지 문구를 무시할 때의 최후 수단."""
    prefix = f"{npc_id}: ".casefold()
    prevs = [ln[len(prefix):].strip() for ln in chat_history.splitlines() if ln.casefold().startswith(prefix)]
    old_norms = _history_sentences(prevs)
    pieces = re.findall(r"[^.?!。\n]+[.?!。]*", speech)  # 문장부호 포함 조각
    kept = [p.strip() for p in pieces if not _sentence_repeated(p, old_norms)]
    removed = [p.strip() for p in pieces if _sentence_repeated(p, old_norms)]
    rest = " ".join(kept)
    if not removed or len(_norm_speech(rest)) < REPEAT_STRIP_MIN_CHARS:
        return speech, []
    return rest, removed


async def _call_stage1(
    system_prompt: str,
    user_content: str,
    ctx: _Stage1Context,
    npc_id: str,
    temperature: float,
    log_extra: dict | None,
    allow_fallback: bool = True,
) -> DialogueResponse | None:
    """Stage1 e4b 호출 — 구조화 해피패스 → (allow_fallback 시) 구조화 폴백(target enum 없음). 실패면 None."""
    try:
        # 구조화 출력: actions 필드를 스키마에 박아 e4b 가 액션을 빠뜨리지 못하게 강제.
        # Ollama format 직접 호출(langchain with_structured_output 우회) — 실측 2.9배 빠름.
        return await ollama_structured(
            system_prompt,
            user_content,
            DialogueResponse,
            # valid_targets 있으면 target enum grammar 강제 (없으면 None → 기본 스키마).
            schema_override=_dialogue_schema_with_targets(ctx.valid_targets),
            model_name=STAGE1_MODEL,
            # 기본 temp 0.5: 미사여구 드리프트 억제(Tier2). 0.4 는 반복적, 0.6 는 과격/장황 드리프트 —
            # 스윕 결과 0.5 가 자연스러움·다양성·캐릭터 유지 균형점(실측).
            temperature=temperature,
            num_predict=300,
            log_extra=log_extra,
        )
    except Exception as e:
        logger.warning(f"[Dialogue] Stage1 LLM 오류 ({npc_id}), 구조화 폴백 재시도: {e}")
        if not allow_fallback:
            return None
    # 폴백도 구조화: target enum 없이(폴백은 valid_targets 미보장) 기본 스키마 + temp↑(다양성).
    try:
        return await ollama_structured(
            system_prompt,
            user_content,
            DialogueResponse,
            model_name=STAGE1_MODEL,
            temperature=max(0.7, temperature),
            num_predict=300,
            log_extra={**log_extra, "fallback": True} if log_extra else None,
        )
    except Exception as e2:
        logger.warning(f"[Dialogue] Stage1 폴백 재시도 실패 ({npc_id}), 기본 응답: {e2}")
        return None


def _clean_stage1_resp(resp: DialogueResponse) -> None:
    """byte-fallback 토큰 정제 — 구조화 obj 필드에 in-place(정규식 텍스트 왕복 없이 parity).
    cross-module 정제 중복 회피: interface_output 은 이미 정제된 speech 가정."""
    resp.speech = _decode_byte_tokens((resp.speech or "").strip()) or "..."
    resp.tone = _decode_byte_tokens((resp.tone or "").strip())


async def _run_stage1_llm(
    ctx: _Stage1Context, npc_id: str, log_extra: dict | None = None
) -> tuple[DialogueResponse, bool]:
    """Stage1 e4b 호출 — 구조화 해피패스 → 구조화 폴백(target enum 없음) → 기본 응답.
    전 경로가 DialogueResponse 산출(자유텍스트 파싱층 제거). 반환: (resp, plan_achieved).
    새 speech 가 같은 NPC 의 직전 발화 기록과 거의 같으면 온도를 올려 정확히 1회만 재호출한다."""
    structured_content = DIALOGUE_STRUCTURED_PROMPT.format(**ctx.fmt_kwargs)
    # 파티 어휘 꼬리는 초대/해산 발화이거나 대상이 파티원일 때만 — 매 턴 붙이면 프롬프트만 불어난다.
    if detect_party_intent(ctx.clean_query) or npc_id in STATE.party_members:
        structured_content += "\n" + PARTY_VOCAB_PROMPT
    if STATE.party_members:
        structured_content += "\n" + PARTY_PROMPT_LINE.format(members=", ".join(sorted(STATE.party_members)))
    user_content = f"Context: {ctx.natural_context}"

    logger.info(f"[Dialogue] Stage1 e4b: {ctx.fmt_kwargs['name']} | '{ctx.natural_context[:50]}...'")

    resp = await _call_stage1(structured_content, user_content, ctx, npc_id, 0.5, log_extra)
    if resp is None:
        resp = DialogueResponse(mode="Common", facial="Neutral", speech="...", tone="confused", actions=[])
    _clean_stage1_resp(resp)

    # 직전 발화 복사 방지(대사 확정 단계 — 파티 의도 보정·게이트보다 먼저). 재호출도 반복이면 그대로 쓴다.
    prev = _repeated_line(resp.speech, str(ctx.fmt_kwargs.get("chat_history", "")), npc_id)
    if prev:
        logger.info(f"[Dialogue] 직전 발화 반복 감지 ({npc_id}), 1회 재생성: '{prev[:40]}'")
        retry = await _call_stage1(
            structured_content,
            f'{user_content}\n직전 문장을 반복하지 말 것: "{prev}"',
            ctx,
            npc_id,
            REPEAT_RETRY_TEMPERATURE,
            # repeat_retry/repeat_discarded_speech: 같은 턴의 첫 행(버려진 원 응답)을 학습 데이터 추출 때
            # 거를 수 있게 하는 표식 — discarded_speech 와 같은 speech 를 가진 같은 msg_id/npc_id 행이 폐기 대상.
            {**log_extra, "repeat_retry": True, "repeat_discarded_speech": resp.speech} if log_extra else None,
            allow_fallback=False,  # 재생성은 해피패스 1회만 — 최악 호출 수 = 원래 경로 + 1
        )
        if retry is not None:
            _clean_stage1_resp(retry)
            resp = retry
        # 재생성도(또는 재생성 실패로 유지된 원 응답도) 문장 반복이면 그 문장만 잘라낸다 — 기록 전에 해야
        # 반복이 기록에 더 쌓이지 않는다. 남는 대사가 너무 짧으면 그대로 둔다.
        stripped, removed = _strip_repeated_sentences(resp.speech, str(ctx.fmt_kwargs.get("chat_history", "")), npc_id)
        if removed:
            logger.info(f"[Dialogue] 반복 문장 제거 ({npc_id}): {removed}")
            resp.speech = stripped

    plan_achieved = bool(resp.plan_achieved)
    if plan_achieved:
        logger.info(f"[Dialogue] Stage1 plan 달성 감지 ({npc_id})")
    logger.info(f"[Dialogue] Stage1 응답 ({npc_id}): '{resp.speech[:60]}...'")
    return resp, plan_achieved


def _record_dialogue_memory(npc_id: str, resp: DialogueResponse, clean_query: str, natural_context: str) -> None:
    """resp.speech 를 fire-and-forget 메모리 기록 (구조화 obj 직접 사용, 정규식 추출 제거)."""
    speech_for_memory = (resp.speech or "").strip()[:100]
    memory_input = clean_query if clean_query else natural_context

    spawn_background(
        asyncio.to_thread(add_conversation, npc_id, memory_input, speech_for_memory),
        label="dialogue-memory",
    )


async def _dialogue_single(state: AgentState, npc_id: str) -> tuple[str, DialogueResponse, bool]:
    """
    Stage 1: 단일 NPC에 대한 e4b 호출. 반환: (npc_id, resp, plan_achieved).
    수집(_collect_stage1_context) → 생성(_run_stage1_llm) → 기록(_record_dialogue_memory).
    """
    ctx = await _collect_stage1_context(state, npc_id)
    # 파인튜닝 로그 컨텍스트 — msg_id 로 Rules 레코드와 조인 (train_logger)
    log_extra = {
        "stage": "stage1",
        "msg_id": state.get("msg_id", ""),
        "npc_id": npc_id,
        "attempt": state.get("rules_retry_count", 0),
        "valid_targets": ctx.valid_targets or [],
    }
    resp, plan_achieved = await _run_stage1_llm(ctx, npc_id, log_extra)
    player_id = state["vr_context"].player_id or "Player"
    # 의도 매핑(Follow→JoinParty·Stop 보호)을 게이트 앞에서 — 치환 결과가 호감도·정원 게이트를 거치게.
    _apply_party_intent(resp, npc_id, ctx.clean_query, player_id)
    # 메모리 기록 전에 게이트를 건다 — 거절된 수락 대사가 기억에 남으면 이후 턴이 "수락했다" 로 이어진다.
    await _gate_party_actions(resp, npc_id, player_id)
    _record_dialogue_memory(npc_id, resp, ctx.clean_query, ctx.natural_context)
    return npc_id, resp, plan_achieved


def _story_directive_block(story_directive: Optional[Dict[str, dict]], npc_ids) -> str:
    """스토리 디렉터 goal/hint → Stage2 sections 앞 블록. 대상 NPC 에 해당하는 항목 없으면 ""."""
    if not story_directive:
        return ""
    id_map = ci_id_map(npc_ids)
    lines = []
    for npc, d in story_directive.items():
        real = id_map.get(npc.strip().lower())
        if real and d.get("goal"):
            lines.append(f"{real}: {d['goal']}" + (f" — {d['hint']}" if d.get("hint") else ""))
    return "=== STORY DIRECTIVE ===\n" + "\n".join(lines) + "\n\n" if lines else ""


async def _generate_plans(
    raw_responses: Dict[str, str],
    player_id: str,
    msg_id: str = "",
    story_directive: Optional[Dict[str, dict]] = None,
) -> Dict[str, dict]:
    """
    Stage 2: plan 전용 산출. 재계획(requires_replan=True) 경로에서만 호출.
    대사는 건드리지 않음 — Stage1 출력이 그대로 최종 (정제는 Stage1 프롬프트가 담당).
    grammar 강제(PlanBatchResponse)로 goal/steps 형식 보장 — 텍스트 [Plan:] 파싱 제거.
    story_directive: 스토리 디렉터 캐시(npc→{goal,hint}) — 있으면 sections 앞에 STORY DIRECTIVE 블록.
    반환: npc_plans npc_id→{goal, steps, relation_snapshot}. 실패 시 {} (plan 생략).
    """
    sections = _story_directive_block(story_directive, raw_responses) + "\n\n".join(
        f"=== NPC: {npc_id} ===\n{raw}" for npc_id, raw in raw_responses.items()
    )

    logger.info(f"[Dialogue] Stage2 plan 산출 시작 ({len(raw_responses)}개 NPC)")
    try:
        result = await stage2_structured(
            PLAN_SYSTEM_PROMPT,
            sections,
            PlanBatchResponse,
            temperature=0.3,
            # plan-only 는 NPC 당 ~100토큰 (goal 1구절 + steps 2-4개). 여유 2배.
            num_predict=220 * len(raw_responses),
            log_extra={"stage": "stage2", "msg_id": msg_id, "npc_ids": list(raw_responses)},
        )
    except Exception as e:
        logger.error(f"[Dialogue] Stage2 오류, plan 생략: {e}")
        return {}

    # npc_id 매칭: 12B 가 헤더를 그대로 복사하지만 대소문자 흔들림 대비 lower 매핑.
    id_map = ci_id_map(raw_responses)
    npc_plans: Dict[str, dict] = {}
    for item in result.npcs:
        npc_id = id_map.get(item.npc_id.strip().lower())
        if npc_id is None:
            logger.warning(f"[Dialogue] Stage2 미상 npc_id 무시: '{item.npc_id}'")
            continue
        # 12B 가 간혹 "1. " 번호 접두사를 붙임 — 제거 (실측).
        steps = [re.sub(r"^\s*\d+[.)]\s*", "", s).strip() for s in item.steps if s.strip()]

        # 12B 가 goal 자리에 npc_id 를 그대로 넣는 일이 잦다(2026-09-05 실측: 9/9).
        # 프롬프트 지시 강화·필드 설명 보강·필드명 변경 셋 다 효과 없었다. 그대로 두면
        # Stage1 컨텍스트에 "Current goal: Moca" 가 실려 다음 턴 대사를 망친다.
        # steps 는 대체로 쓸 만하므로 첫 step 으로 대체하고, 그마저 오염이면 plan 을 버린다.
        goal = item.goal.strip()
        if goal.casefold() == item.npc_id.strip().casefold():
            goal = steps[0] if steps else ""
            logger.info(f"[Dialogue] Stage2 goal 이 npc_id 와 동일 — 첫 step 으로 대체 ({npc_id}): '{goal}'")

        # steps 가 스키마 필드명을 그대로 뱉은 경우(실측: ["Moca","goal","steps"])도 버린다.
        FIELD_ECHO = {"goal", "steps", "npc_id", item.npc_id.strip().casefold()}
        steps = [st for st in steps if st.casefold() not in FIELD_ECHO]

        if not goal or not steps:
            logger.warning(f"[Dialogue] Stage2 plan 오염으로 폐기 ({npc_id})")
            continue

        plan = {"goal": goal, "steps": steps}
        # relation_snapshot: affinity score만 (확정 결정). 조회 실패 시 0.
        try:
            relation = await db_manager.get_affinity(npc_id, player_id)
            plan["relation_snapshot"] = relation.affinity_score if relation else 0
        except Exception as e:
            logger.error(f"[Dialogue] plan affinity 조회 실패 ({npc_id}): {e}")
            plan["relation_snapshot"] = 0
        npc_plans[npc_id] = plan

    for npc_id in raw_responses:
        if npc_id not in npc_plans:
            logger.warning(f"[Dialogue] Stage2 plan 누락 ({npc_id})")
    logger.info(f"[Dialogue] Stage2 plan 산출 완료 ({len(npc_plans)}/{len(raw_responses)}개)")
    return npc_plans


async def dialogue_node(state: AgentState):
    """
    Dialogue Agent (3-Stage Multi-NPC).

    Stage 1: e4b × N 병렬 (지식 격리, 각 NPC 독립 호출) — 대사 최종본
    Stage 2: 플래너 × 1 plan 산출 (requires_replan=True 시만, 대사 무변경)
    Output:  structured_responses Dict[npc_id, DialogueResponse]
    """
    npcs = state.get("target_npcs") or [state.get("target_npc") or DEFAULT_NPC]

    # 재계획 분기: False=e4b 단독 경량 루프(플래너 스킵), True=풀 파이프라인+plan 산출.
    requires_replan = state.get("requires_replan", True)
    logger.info(f"[Dialogue] 대상 NPC: {npcs} | requires_replan={requires_replan}")

    # Stage 1: 병렬 e4b 호출 — 구조화 DialogueResponse 직접 산출.
    results = await asyncio.gather(*[_dialogue_single(state, npc_id) for npc_id in npcs])
    structured_responses: Dict[str, DialogueResponse] = {r[0]: r[1] for r in results}
    plan_achieved_map: Dict[str, bool] = {r[0]: r[2] for r in results}
    single_npc = npcs[0]

    # Stage 2: plan 산출 — 재계획 시에만. 경량 루프는 e4b 단독으로 종료.
    # 대사는 Stage1 출력 그대로 (플래너 정제 제거 — PLAN_SYSTEM_PROMPT 상단 주석 참조).
    # raw_responses(텍스트) 직렬화는 유일 소비처인 Stage2 plan 입력용 — replan 턴에만.
    # 경량 루프는 소비처 없음(Stage3 는 structured, 메모리는 resp.speech 직접) → 빈 dict.
    npc_plans: Dict[str, dict] = {}
    raw_responses: Dict[str, str] = {}
    if requires_replan:
        raw_responses = {npc_id: _serialize_dialogue(resp) for npc_id, resp in structured_responses.items()}
        player_id = state["vr_context"].player_id or "Player"
        npc_plans = await _generate_plans(
            raw_responses, player_id, state.get("msg_id", ""), state.get("story_directive")
        )
    else:
        logger.info("[Dialogue] 경량 루프: Stage2 스킵 (e4b 단독)")

    return {
        "structured_responses": structured_responses,
        "raw_responses": raw_responses,
        "npc_plans": npc_plans or None,
        "plan_achieved": plan_achieved_map or None,
        "target_npc": single_npc,
        "current_speaker": "Dialogue",
        "next": "Interface_Output",
    }
