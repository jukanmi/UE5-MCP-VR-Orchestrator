"""Stage1 대화 모델 비교 벤치 — 파인튜닝 버전 선택·회귀 검출용.

프로덕션과 같은 경로로 프롬프트를 조립한다(_collect_stage1_context →
DIALOGUE_STRUCTURED_PROMPT). 모델만 갈아끼우므로 결과 차이는 모델 차이다.

대화 기록을 반드시 통제한다. 오염된 기록이 있으면 모델이 직전 답변 문형을 복사해
모델 자체 성능이 가려진다(2026-09-05 실측: 같은 모델이 기록 유무로 GiveItem 2/2 ↔
0/2 로 뒤집혔다). --seed-history 로 오염 상태를, 생략하면 백지 상태를 만든다.

사용:
    python tools/finetune_eval/stage1_bench.py --models gemma4-e4b-dialogue-v4 qwen3:8b
    python tools/finetune_eval/stage1_bench.py --models ... --seed-history --runs 3
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
from app.agents.subgraphs.dialogue import (                              # noqa: E402
    _collect_stage1_context, _dialogue_schema_with_targets,
)
from app.agents.subgraphs.prompts import DIALOGUE_STRUCTURED_PROMPT      # noqa: E402
from app.schemas.actions import DialogueResponse                         # noqa: E402

NPC = "Moca"
MEM_PATH = os.path.join("app", "agents", "knowledge", NPC.lower(), "conversation_memory.json")

INVENTORY = [
    {"id": "Rock", "name": "돌멩이", "count": 1},
    {"id": "Bandage", "name": "붕대", "count": 1},
    {"id": "WaterSkin", "name": "가죽 물통", "count": 1},
]
VALID_TARGETS = ["Player", "Self", "Enemy", NPC, "1002"]
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
]

# 오염 기록 — 실제로 겪은 실패 문형. 모델이 이걸 복사하는지 본다.
POISON = [
    ("가방에 뭐 있어?", "가방에 무엇이 들어있는지 궁금한가요? 그럼 한번 확인해 볼까요."),
    ("붕대 있어?", "붕대요? 그럼 여기 있는 붕대를 한번 확인해 보시겠어요?"),
    ("물통 있어?", "물통이 필요하신가요? 그럼 여기 있는 가죽 물통을 한번 확인해 보시겠어요?"),
]
ECHO_MARK = "확인해 보시겠어요"


def write_history(seed: bool) -> None:
    now = datetime.datetime.now().isoformat()
    entries = []
    if seed:
        for q, a in POISON:
            entries.append({"timestamp": now, "speaker": "Player", "content": q, "is_summary": False})
            entries.append({"timestamp": now, "speaker": NPC, "content": a, "is_summary": False})
    with io.open(MEM_PATH, "w", encoding="utf-8") as f:
        json.dump({"agent_id": NPC, "last_updated": now, "entries": entries}, f,
                  ensure_ascii=False, indent=2)
    MM._memory_cache.clear()   # 파일에서 다시 읽게 — 캐시가 살아 있으면 위 쓰기가 무시된다


def state_for(text: str) -> dict:
    return {
        "target_npcs": [NPC], "target_npc": NPC, "player_id": "Bench",
        "natural_context": 'Player said: "%s".' % text,
        "requires_replan": False,
        "vr_context": {
            "npc_inventory": {NPC: INVENTORY},
            "valid_targets": VALID_TARGETS,
            "nearby_furniture": FURNITURE,
            "voice_transcript": text,
            "player_id": "Bench",
        },
    }


async def main(models, runs, seed_history):
    write_history(seed_history)

    prompts = {}
    for question, _ in CASES:
        ctx = await _collect_stage1_context(state_for(question), NPC)
        prompts[question] = (
            DIALOGUE_STRUCTURED_PROMPT.format(**ctx.fmt_kwargs),
            "Context: " + ctx.natural_context,
            _dialogue_schema_with_targets(ctx.valid_targets),
        )

    print("대화 기록: %s" % ("오염 6항목 주입" if seed_history else "백지"))
    for tag in models:
        alias = "bench_" + tag.replace(":", "_").replace("-", "_")
        LF.MODELS[alias] = tag
        print("\n" + "=" * 70 + "\n## %s" % tag)
        for question, expect in CASES:
            system, user, schema = prompts[question]
            print('\n  "%s"   기대: %s' % (question, expect))
            for i in range(runs):
                started = time.time()
                try:
                    resp = await LF.ollama_structured(
                        system, user, DialogueResponse, schema_override=schema,
                        model_name=alias, temperature=0.5, num_predict=300, timeout=300,
                    )
                    actions = [
                        {k: v for k, v in a.model_dump(exclude_none=True).items() if v}
                        for a in (resp.actions or [])
                    ]
                    echo = " [문형복사]" if ECHO_MARK in (resp.speech or "") else ""
                    print("    #%d [%.1fs]%s %s" % (i + 1, time.time() - started, echo, resp.speech))
                    print("        %s" % actions)
                except Exception as e:  # noqa: BLE001
                    print("    #%d 실패: %s: %s" % (i + 1, type(e).__name__, str(e)[:140]))


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--models", nargs="+", required=True, help="Ollama 태그 (예: gemma4-e4b-dialogue-v4)")
    p.add_argument("--runs", type=int, default=2)
    p.add_argument("--seed-history", action="store_true", help="오염된 대화 기록을 주입해 복사 내성을 본다")
    args = p.parse_args()
    asyncio.run(main(args.models, args.runs, args.seed_history))
