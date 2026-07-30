#!/usr/bin/env python
"""광역 eval — 학습 정확 재현(카테고리별) + held-out 신규 발화(overfit 판별).

1) 학습 데이터에서 카테고리 다양 샘플 8개 뽑아 정확한 system/user 로 신/구 모델 비교
   → 액션 타입 일치율(Rules 생존 근사).
2) 학습셋에 없는 신규 발화 6개(다른 페르소나·다른 상황 phrasing) → 다양성/일반화 확인.
   overfit 이면 speech 가 학습 문구를 그대로 복붙하거나 액션이 엉뚱하게 나옴.
"""
import argparse
import json
import random
import re
import urllib.request

# 비교 대상·출력 경로 인자화 — 재학습마다 태그가 늘어나므로(v1, v2, ...) 하드코딩이면
# 매번 파일을 고쳐야 한다. 기본값은 기존 동작(구모델 vs v1) 그대로.
_ap = argparse.ArgumentParser()
_ap.add_argument("--models", nargs="+", default=["gemma4:e4b", "gemma4-e4b-dialogue-v1"],
                 help="비교할 ollama 태그 2개 이상 (예: gemma4-e4b-dialogue-v1 gemma4-e4b-dialogue-v2)")
_ap.add_argument("--data", default="data/processed/stage1_e4b_train.jsonl")
_ap.add_argument("--out", default="eval/broad_result.json")
_args = _ap.parse_args()

random.seed(7)

ROWS = [json.loads(l) for l in open(_args.data, encoding="utf-8")]


def call(model: str, sys_m: str, usr_m: str, temp: float = 0.4) -> dict:
    body = json.dumps({
        "model": model,
        "messages": [{"role": "system", "content": sys_m}, {"role": "user", "content": usr_m}],
        "stream": False,
        "options": {"temperature": temp},
    }).encode()
    req = urllib.request.Request("http://127.0.0.1:11434/api/chat", data=body,
                                 headers={"Content-Type": "application/json"})
    raw = json.loads(urllib.request.urlopen(req, timeout=120).read())["message"]["content"]
    m = re.search(r"\{.*\}", raw, re.S)
    try:
        return json.loads(m.group(0)) if m else {"_raw": raw[:150]}
    except Exception:
        return {"_raw": raw[:150]}


def act_types(d: dict) -> list:
    out = []
    for a in (d.get("actions") or []):
        out.append(a.get("type") if isinstance(a, dict) else str(a))
    return out


MODELS = _args.models
report = {"1_gold_reproduction": [], "2_heldout_generalization": []}

# ── 1) 학습 카테고리 다양 샘플 (정확 재현율) ──
by_type = {}
for d in ROWS:
    try:
        gold = json.loads(d["messages"][2]["content"])
        for a in (gold.get("actions") or []):
            t = a.get("type") if isinstance(a, dict) else str(a)
            by_type.setdefault(t, []).append(d)
    except Exception:
        pass

pick_types = ["Sleep", "Attack", "GiveItem", "Move", "Comfort", "Trade", "Investigate", "Follow"]
samples = []
for t in pick_types:
    cands = by_type.get(t) or []
    if cands:
        samples.append((t, random.choice(cands)))

for t, d in samples:
    sys_m, usr_m = d["messages"][0]["content"], d["messages"][1]["content"]
    gold = json.loads(d["messages"][2]["content"])
    row = {"category": t, "user": usr_m[-90:], "gold_actions": act_types(gold)}
    for model in MODELS:
        out = call(model, sys_m, usr_m, temp=0.2)
        row[model] = {"actions": act_types(out), "speech": (out.get("speech") or out.get("_raw", ""))[:80]}
    report["1_gold_reproduction"].append(row)

# ── 2) held-out 신규 발화 (학습셋에 없는 phrasing, 페르소나 Moca 고정) ──
SYS_MOCA = (
    "You are Moca, a ASMR Streamer, an NPC in a VR game. Personality traits: Gentle, "
    "Soft-spoken, Attentive, Soothing. Respond ONLY as a JSON object with fields: mode, "
    "facial, speech, tone, actions, plan_achieved. A command like 앉아/누워/따라와 tells "
    "YOU to do it — YOU perform it. Available action types: Move Follow TurnTo Wait Stop "
    "Scan Idle UseItem Equip Unequip Attack Block Dodge Flee SignalAllies Trade GiveItem "
    "HandObject Comfort Emote PickUp Drop Craft Repair Investigate Track Scout Sit Sleep "
    "Read Pray Dance Sing. target MUST be one of: Player, Self, 1001, Bed_01."
)
HELDOUT = [
    "졸려서 좀 자야겠어, 침대 좀 써도 될까",
    "나 대신 저기 있는 상자 좀 확인해줄래",
    "노래 한 곡 불러줄 수 있어?",
    "요즘 몸이 안 좋아서 그런지 계속 피곤하네, 좀 눕고 싶어",
    "저기 의자에 앉아서 얘기하자",
    "잠깐 자리 좀 비켜줄래? 여기서 좀 쉬고 싶은데",
]
for u in HELDOUT:
    row = {"utterance": u}
    for model in MODELS:
        out = call(model, SYS_MOCA,
                   f'Context: Player said: "{u}". Nearby furniture you can use as Sit/Sleep target: '
                   f'1001 (Seat, vacant, 2m); Bed_01 (Bed, vacant, 2m)', temp=0.5)
        row[model] = {"actions": act_types(out), "speech": (out.get("speech") or out.get("_raw", ""))[:100]}
    report["2_heldout_generalization"].append(row)

with open(_args.out, "w", encoding="utf-8") as fp:
    json.dump(report, fp, ensure_ascii=False, indent=2)
print(f"WRITTEN {_args.out}")
