"""Stage1 대화 모델 비교 벤치 — 파인튜닝 버전 선택·회귀 검출용.

프로덕션과 같은 경로로 프롬프트를 조립한다(_collect_stage1_context →
DIALOGUE_STRUCTURED_PROMPT). 모델만 갈아끼우므로 결과 차이는 모델 차이다.

실행 중 Moca 기록(conversation_memory.json)을 임시로 바꾼다 — 서버를 띄운 채 돌리지 말 것. 종료·예외 시 원복한다.

대화 기록을 반드시 통제한다. 오염된 기록이 있으면 모델이 직전 답변 문형을 복사해
모델 자체 성능이 가려진다(2026-09-05 실측: 같은 모델이 기록 유무로 GiveItem 2/2 ↔
0/2 로 뒤집혔다). --seed-history 로 오염 상태를, 생략하면 백지 상태를 만든다.

파티·POI 케이스(--suite new)는 기대 액션을 자동 채점한다. "원시" = 모델 출력 그대로,
"보정후" = 서버 _apply_party_intent(초대 Follow→JoinParty, 해산→LeaveParty, 파티원 Stop→Idle) 통과 후.
--prod-tail 은 운영이 프롬프트 끝에 붙이는 파티 지시·일행 줄과 user 의 파티 힌트까지 재현한다.

사용:
    python tools/finetune_eval/stage1_bench.py --models gemma4-e4b-dialogue-v4 qwen3:8b
    python tools/finetune_eval/stage1_bench.py --models ... --seed-history --runs 3
    python tools/finetune_eval/stage1_bench.py --models ... --suite new --prod-tail --seed-history
"""
import argparse
import asyncio
import datetime
import io
import json
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = os.path.join(REPO, "OmniAgent_VR_System", "CognitiveEngine")
sys.path.insert(0, ENGINE)
os.chdir(ENGINE)

from app.utils import llm_factory as LF                                  # noqa: E402
from app.utils import memory_manager as MM                               # noqa: E402
from app.agents.interface_input import _PARTY_HINTS, _format_known_pois  # noqa: E402
from app.agents.party_intent import detect_party_intent                  # noqa: E402
from app.agents.subgraphs.dialogue import (                              # noqa: E402
    _apply_party_intent, _collect_stage1_context, _dialogue_schema_with_targets, _repeated_line,
)
from app.agents.subgraphs.prompts import (                               # noqa: E402
    DIALOGUE_STRUCTURED_PROMPT, PARTY_PROMPT_LINE, PARTY_VOCAB_PROMPT,
)
from app.schemas.actions import DialogueResponse                         # noqa: E402
from app.schemas.vr_context import GesPrompt                             # noqa: E402
from app.server_state import STATE                                       # noqa: E402

NPC = "Moca"
MEM_PATH = os.path.join("app", "agents", "knowledge", NPC.lower(), "conversation_memory.json")

# 12종 — 목록이 길어져도 ItemID 가 유지되는지 함께 본다(3종에서만 재면 과대평가된다).
INVENTORY = [
    {"id": "Rock", "name": "돌멩이", "count": 5},
    {"id": "Bandage", "name": "붕대", "count": 2},
    {"id": "WaterSkin", "name": "가죽 물통", "count": 1},
    {"id": "HealthPotion", "name": "치유 물약", "count": 3},
    {"id": "ManaPotion", "name": "마나 물약", "count": 1},
    {"id": "KnightSword", "name": "기사의 검", "count": 1},
    {"id": "Shield_Knight", "name": "기사의 방패", "count": 1},
    {"id": "Torch", "name": "횃불", "count": 1},
    {"id": "HerbTea", "name": "약초 차", "count": 1},
    {"id": "SmokeBomb", "name": "연막 구슬", "count": 2},
    {"id": "Lockpick", "name": "자물쇠 따개", "count": 1},
    {"id": "Rations", "name": "건량", "count": 4},
]
# Elara — 플레이어가 아닌 대상 지정이 되는지 보기 위한 타 NPC.
VALID_TARGETS = ["Player", "Self", "Enemy", NPC, "Elara", "1002"]
FURNITURE = [{"id": "1002", "type": "Bed", "occupied": False, "dist_m": 3.2}]

# (플레이어 발화, 기대 동작). 기대는 사람이 읽고 판정하는 라벨이다 — 자동 채점은
# 하지 않는다. 대사 품질은 문자열 비교로 잴 수 없고, 액션도 상황에 따라 복수 정답이 있다.
CASES = [
    ("돌 좀 줘",                    "GiveItem/Rock — 소유권 이전"),
    ("붕대 하나 건네줘",             "GiveItem/Bandage — 소유권 이전"),
    ("빵 좀 줘",                    "미보유. 없다고 말하고 액션 없음"),
    ("가방에 뭐 있어?",              "보유 아이템을 실제로 나열. 액션 불필요"),
    ("나 따라와",                   "Follow/Player"),
    ("침대에 누워",                 "Sleep/1002"),
    ("그 돌 좀 보여줘",              "HandObject/Rock — 소유권 유지"),
    ("밖에서 큰 소리 나는데 괜찮아?",  "대사만. 액션 없음"),
    # --- 2차 축: 수량·거래·전투·자기사용·타 NPC 대상 (2026-09-05 추가) ---
    ("돌 3개만 줘",                 "GiveItem/Rock + amount=3 — 수량 필드"),
    ("돌 줄테니 빵이랑 바꾸자",       "Trade — give_item_id=Rock, get_item_id=Bread 양쪽"),
    ("적이다! 싸워!",               "mode=Combat + Attack/Enemy"),
    ("위험해, 도망쳐!",              "mode=Combat + Flee"),
    ("검 장착해",                   "Equip/KnightSword"),
    ("목마르면 물 마셔",             "UseItem/WaterSkin target=Self — 자기사용 (GiveItem 아님)"),
    ("치유 물약 하나 줘",            "GiveItem/HealthPotion — 12종 목록에서 ItemID 유지"),
    ("엘라라한테 붕대 좀 갖다줘",     "GiveItem target=Elara — 플레이어가 아닌 대상"),
]

# 이름 있는 장소 — UE5 가 known_pois 로 보내는 꼴({id,name,aliases}).
POIS = [
    {"id": "Well", "name": "우물", "aliases": ["샘"]},
    {"id": "Market", "name": "시장", "aliases": ["장터"]},
    {"id": "Gate", "name": "성문", "aliases": []},
]


def _has(action_type: str, **fields: str):
    """액션 중 type 과 지정 필드가 맞는 게 있으면 통과."""
    return lambda acts: any(
        a.get("type") == action_type and all(a.get(k) == v for k, v in fields.items()) for a in acts)


def _lacks(*action_types: str):
    """액션 중 지정 type 이 하나도 없으면 통과(Idle·무액션 모두 허용)."""
    return lambda acts: not any(a.get("type") in action_types for a in acts)


# 파티·POI 케이스 (2026-10-08 추가, docs/PROPOSAL_npc_llm_jev.md P1·P2). 자동 채점 — check(actions)->bool.
#  party=True: 대상 NPC 가 이미 일행. pois=True: 프롬프트에 known_pois 를 싣는다. sentiment: 호감도 줄 덮어쓰기.
#  corrected=False: 서버 보정 채점 제외(거절은 운영에서 호감도 게이트가 JoinParty 를 거르므로 원시 출력만 본다).
NEW_CASES = [
    dict(q="같이 가자", expect="JoinParty (학습상 Follow 정답 — 서버가 초대 Follow→JoinParty 보정)",
         check=_has("JoinParty")),
    dict(q="우리 일행이 되어줘", expect="JoinParty", check=_has("JoinParty")),
    dict(q="우리 일행에 합류해서 같이 가자", expect="JoinParty (2026-10-08 PIE 에서 Follow+직전 발화 복사로 실패한 발화)",
         check=_has("JoinParty")),
    dict(q="우리 일행이 되어줘", sentiment="Hostile (Score: -60)", corrected=False,
         expect="거절 — 대사로 거절, JoinParty 없음(원시 기준)", check=_lacks("JoinParty")),
    dict(q="이제 헤어지자", party=True, expect="LeaveParty (파티원)", check=_has("LeaveParty")),
    dict(q="멈춰", party=True, expect="Idle/무액션 — Stop·LeaveParty 면 실패(Stop=파티 해산)",
         check=_lacks("Stop", "LeaveParty", "JoinParty")),
    dict(q="우물로 가", pois=True, expect="Move poi=Well", check=_has("Move", poi="Well")),
    dict(q="시장에 가서 구경 좀 해", pois=True, expect="Move poi=Market (간접 표현)", check=_has("Move", poi="Market")),
]

# 오염 기록 — 실제로 겪은 실패 문형. 모델이 이걸 복사하는지 본다.
POISON = [
    ("가방에 뭐 있어?", "가방에 무엇이 들어있는지 궁금한가요? 그럼 한번 확인해 볼까요."),
    ("붕대 있어?", "붕대요? 그럼 여기 있는 붕대를 한번 확인해 보시겠어요?"),
    ("물통 있어?", "물통이 필요하신가요? 그럼 여기 있는 가죽 물통을 한번 확인해 보시겠어요?"),
]
ECHO_MARK = "확인해 보시겠어요"
# 직전 NPC 발화·이벤트 — 2026-10-08 PIE 에서 응답이 글자까지 복사한 줄과, 기록을 채우던 Event 줄.
LAST_EXCHANGE = ("maybe?", "그래, 아직도 남아있어.")
EVENT_LINE = "근처에서 큰 소리가 나서 반사적으로 몸을 낮췄다."


def write_history(seed: bool) -> None:
    now = datetime.datetime.now().isoformat()
    entries = []
    if seed:
        for q, a in POISON:
            entries.append({"timestamp": now, "speaker": "Player", "content": q, "is_summary": False})
            entries.append({"timestamp": now, "speaker": NPC, "content": a, "is_summary": False})
        entries.append({"timestamp": now, "speaker": "Event", "content": EVENT_LINE, "is_summary": False})
        for who, line in zip(("Player", NPC), LAST_EXCHANGE):
            entries.append({"timestamp": now, "speaker": who, "content": line, "is_summary": False})
    with io.open(MEM_PATH, "w", encoding="utf-8") as f:
        json.dump({"agent_id": NPC, "last_updated": now, "entries": entries}, f,
                  ensure_ascii=False, indent=2)
    MM._memory_cache.clear()   # 파일에서 다시 읽게 — 캐시가 살아 있으면 위 쓰기가 무시된다


def state_for(text: str, pois: bool = False, prod_tail: bool = False) -> dict:
    # 서빙은 vr_context 를 GesPrompt 객체로 정규화해 넘긴다(dict 면 _collect_stage1_context 가 AttributeError).
    vr = GesPrompt(
        timestamp=time.time(), player_id="Bench", voice_transcript=text,
        npc_inventory={NPC: INVENTORY}, valid_targets=VALID_TARGETS, nearby_furniture=FURNITURE,
        known_pois=POIS if pois else None,
    )
    natural = 'Player said: "%s".' % text + _format_known_pois(vr)
    hint = _PARTY_HINTS.get(detect_party_intent(text)) if prod_tail else None
    if hint:
        natural += ". Hint: " + hint   # interface_input._build_natural_context 와 같은 꼬리
    return {
        "target_npcs": [NPC], "target_npc": NPC, "player_id": "Bench",
        "natural_context": natural, "requires_replan": False, "vr_context": vr,
    }


def prod_system_tail(text: str) -> str:
    """dialogue._run_stage1_llm 이 system 끝에 붙이는 파티 꼬리 재현(그쪽이 비공개 인라인이라 복제)."""
    tail = ""
    if detect_party_intent(text) or NPC in STATE.party_members:
        tail += "\n" + PARTY_VOCAB_PROMPT
    if STATE.party_members:
        tail += "\n" + PARTY_PROMPT_LINE.format(members=", ".join(sorted(STATE.party_members)))
    return tail


def set_party(member: bool) -> None:
    STATE.party_members.clear()
    if member:
        STATE.party_members.add(NPC)


def dump_actions(resp: DialogueResponse) -> list:
    return [{k: v for k, v in a.model_dump(exclude_none=True).items() if v} for a in (resp.actions or [])]


def selected_cases(suite: str) -> list:
    """(질문, 기대 라벨, 옵션 dict) 목록. base 는 라벨만 있는 수동 판정 케이스, new 는 자동 채점."""
    base = [dict(q=q, expect=e) for q, e in CASES]
    return {"base": base, "new": NEW_CASES, "all": base + NEW_CASES}[suite]


async def main(models, runs, seed_history, suite, prod_tail):
    """Moca 기록 파일(gitignore, 복구 불가)을 백업해 두고 종료·예외 시 반드시 원복한다."""
    existed = os.path.exists(MEM_PATH)
    backup = io.open(MEM_PATH, "rb").read() if existed else b""
    try:
        await _run(models, runs, seed_history, suite, prod_tail)
    finally:
        set_party(False)
        if existed:
            with io.open(MEM_PATH, "wb") as f:
                f.write(backup)
        elif os.path.exists(MEM_PATH):
            os.remove(MEM_PATH)
        MM._memory_cache.clear()


async def _run(models, runs, seed_history, suite, prod_tail):
    write_history(seed_history)
    cases = selected_cases(suite)

    prompts = []
    for c in cases:
        set_party(c.get("party", False))
        ctx = await _collect_stage1_context(state_for(c["q"], c.get("pois", False), prod_tail), NPC)
        if c.get("sentiment"):
            ctx.fmt_kwargs["sentiment"] = c["sentiment"]
        system = DIALOGUE_STRUCTURED_PROMPT.format(**ctx.fmt_kwargs)
        if prod_tail:
            system += prod_system_tail(c["q"])
        prompts.append((
            system,
            "Context: " + ctx.natural_context,
            _dialogue_schema_with_targets(ctx.valid_targets),
            str(ctx.fmt_kwargs["chat_history"]),
        ))

    print("대화 기록: %s" % ("오염(문형 복사·직전 발화·Event) 주입" if seed_history else "백지"))
    print("스위트: %s / 운영 꼬리: %s" % (suite, "포함" if prod_tail else "없음"))
    for tag in models:
        alias = "bench_" + tag.replace(":", "_").replace("-", "_")
        LF.MODELS[alias] = tag
        print("\n" + "=" * 70 + "\n## %s" % tag)
        board = []   # (질문, 원시 통과, 보정후 통과|None, 직전발화반복, 총 시도)
        for c, (system, user, schema, history) in zip(cases, prompts):
            check = c.get("check")
            print('\n  "%s"   기대: %s' % (c["q"], c["expect"]))
            raw_ok = fixed_ok = repeat = done = 0
            for i in range(runs):
                started = time.time()
                try:
                    resp = await LF.ollama_structured(
                        system, user, DialogueResponse, schema_override=schema,
                        model_name=alias, temperature=0.5, num_predict=300, timeout=300,
                    )
                except Exception as e:  # noqa: BLE001
                    print("    #%d 실패: %s: %s" % (i + 1, type(e).__name__, str(e)[:140]))
                    continue
                done += 1
                actions = dump_actions(resp)
                tags = ""
                if ECHO_MARK in (resp.speech or ""):
                    tags += " [문형복사]"
                if _repeated_line(resp.speech or "", history, NPC):
                    repeat += 1
                    tags += " [직전발화반복]"
                if check:
                    set_party(c.get("party", False))
                    fixed = resp.model_copy(deep=True)
                    _apply_party_intent(fixed, NPC, c["q"], "Bench")
                    ok, ok2 = check(actions), check(dump_actions(fixed))
                    raw_ok += ok
                    fixed_ok += ok2
                    tags += " 원시=%s 보정후=%s" % ("OK" if ok else "NG", "OK" if ok2 else "NG")
                print("    #%d [%.1fs]%s mode=%s facial=%s achieved=%s" % (
                    i + 1, time.time() - started, tags, resp.mode, resp.facial, resp.plan_achieved))
                print("        %s" % resp.speech)
                print("        %s" % actions)
            board.append((c, raw_ok, fixed_ok, repeat, done))

        print("\n  -- 요약 (%s) --" % tag)
        print("  %-26s %-8s %-8s %s" % ("질문", "원시", "보정후", "직전발화반복"))
        for c, raw_ok, fixed_ok, repeat, done in board:
            graded = bool(c.get("check"))
            fixed_cell = "-" if not graded or c.get("corrected") is False else "%d/%d" % (fixed_ok, done)
            print("  %-26s %-8s %-8s %s" % (
                (c["q"] + (" [파티원]" if c.get("party") else "") + (" [적대]" if c.get("sentiment") else ""))[:26],
                "%d/%d" % (raw_ok, done) if graded else "-", fixed_cell,
                "%d/%d" % (repeat, done) if seed_history else "-"))


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--models", nargs="+", required=True, help="Ollama 태그 (예: gemma4-e4b-dialogue-v4)")
    p.add_argument("--runs", type=int, default=2)
    p.add_argument("--seed-history", action="store_true", help="오염된 대화 기록을 주입해 복사 내성을 본다")
    p.add_argument("--suite", choices=["base", "new", "all"], default="base",
                   help="base=기존 수동 판정, new=파티·POI 자동 채점, all=둘 다")
    p.add_argument("--prod-tail", action="store_true", help="운영 프롬프트 꼬리(파티 지시·일행 줄·파티 힌트) 포함")
    args = p.parse_args()
    asyncio.run(main(args.models, args.runs, args.seed_history, args.suite, args.prod_tail))
