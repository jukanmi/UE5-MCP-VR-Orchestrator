"""jev_dataset.py — Jev 체크포인트(daily + combat 공용) 학습 데이터 생성·평가. SPEC_jev_daily D7·M2.

흐름:
  gen-daily   무작위 상황(C++ BuildJevDailyQuery 와 같은 형태) → gemma 가 정답 조합(활동+슬롯) → combos_daily.jsonl
  gen-combat  무작위 전투 지표 20개씩 묶어 gemma 가 stance 라벨 → combos_combat.jsonl
  build       조합을 상황 단위로 train/validation/test 분할 → 패스 단위 jevlike JSONL
  eval        test 에서 패스별 top-1 일치율(모델 vs 휴리스틱) + 4패스 지연 — 완료 기준 18·19
  sheet       사람 정답용 상황 시트(골드셋) 출력
  review-sheet 같은 골드셋에 모델 선택을 채운 검수형 시트 — 사람은 부자연스러운 것만 표시·수정(LLM 재호출 없음)

패스 context·옵션 문자열은 서비스의 run_daily_passes 를 그대로 써서 만든다 — 학습·추론 포맷이 어긋날 수 없다.
gen-* 는 이어쓰기: 이미 있는 줄 수만큼 건너뛰고 같은 시드로 이어서 만든다.

실행(CognitiveEngine 에서):
  ../../.venv/Scripts/python.exe finetune/jev/jev_dataset.py gen-daily --count 2000
"""

from __future__ import annotations

import argparse
import csv
import glob
import hashlib
import json
import os
import random
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

ROOT = Path(__file__).resolve().parents[2]  # CognitiveEngine
sys.path.insert(0, str(ROOT))

import httpx  # noqa: E402
import yaml  # noqa: E402

from app.services.jev_service import (  # noqa: E402
    ACTIVITY_SLOTS,
    DEFAULT,
    OPTIONS,
    JevlikeService,
    _softmax,
    build_context,
    heuristic_probs,
    run_daily_passes,
    slot_candidates,
)

DATA = Path(__file__).resolve().parent / "data"
ITEM_REGISTRY = ROOT.parents[1] / "Content" / "Data" / "Items" / "ItemRegistry.csv"
OLLAMA = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11434") + "/api/chat"
# 12b 는 persona 를 구분 못 했다(같은 상황 10명 분포 동일). 26b(MoE) 는 31b 와 구분력 비슷·6배 빠름(3.5s vs 21s/호출, 2026-09-24).
MODEL = os.environ.get("JEV_LABEL_MODEL", "gemma4:26b")

# 레벨에 실제 있는 것 + 암기 방지용 가짜 이름. 레벨 POI 는 POI_Gate·Plaza·Well 3개(ca316c01).
POI_NAMES = ["Gate", "Plaza", "Well", "Market", "Shrine", "Dock", "Tower", "Forge"]
FURNITURE = [
    ("Chair_Library_Reading", "seat"),
    ("Bench_03", "seat"),
    ("Chair_Tavern_02", "seat"),
    ("Stool_Well", "seat"),
    ("Bed_01", "bed"),
    ("Bed_Inn_02", "bed"),
    ("1002", "bed"),
]
# 실제 LLM Stage2 plan goal(train_logs 2026-09, 이름만 든 오류값 제외). C++ 는 40자로 잘라 보낸다.
GOALS = [
    "무기 구매 권유", "용 이야기 흘리기", "무기 추천하기", "용의 존재 암시", "플레이어 보호 및 안내",
    "주인공 격려 및 합류 선언", "요새 공략 플랜 브리핑", "화력 지원 맹세", "플레이어의 상태 확인 및 안내",
    "플레이어 격려", "전투 준비 과시", "성검 조각의 위치 안내", "성검 1차 복구 및 불안함 표출",
    "마왕성 침투 작전 지시", "플레이어의 지식 수준을 평가하기", "플레이어의 존중을 요구하며 대립 태도 유지",
    "플레이어에게 자신의 감정을 전달하기", "퇴로 확보", "파편의 위험성과 복잡성을 설명하기",
    "유적의 마법 파편 연구 현황을 설명하기", "왕국 재건과 작별 인사", "영웅들의 귀환 환영",
    "성문 밖 위험 경고 및 무기 준비 권유",
]
HOSTILE_NPCS = {"commander_vorg", "demonlord", "imp_fiend", "orc_vagron"}
DAILY_ACTS = ["stay", "stand_up", "look_at", "wander", "patrol", "rest", "emote", "use_item", "give_item", "pick_up"]


# ─────────────────────────────── 상황 생성 ───────────────────────────────


def _load_personas() -> List[Dict[str, Any]]:
    """실제 NPC(중복 Inhabitant 1개로) + 합성 풀 300. 실제 NPC 를 절반 비중으로 뽑는다."""
    real: Dict[str, Dict[str, Any]] = {}
    for f in sorted(glob.glob(str(ROOT / "app/agents/personas/generic/*.yaml"))):
        d = yaml.safe_load(open(f, encoding="utf-8")) or {}
        name = Path(f).stem
        if name == "player" or not d.get("role"):
            continue
        key = f"{d['role']}|{d.get('traits')}"
        real.setdefault(key, {"name": name, "role": d["role"], "traits": list(d.get("traits") or [])})
    pool = yaml.safe_load(open(ROOT / "finetune/data/synth/persona_pool.yaml", encoding="utf-8"))
    synth = [{"name": p["name"], "role": p["role"], "traits": list(p.get("traits") or [])} for p in pool]
    return [{"real": True, **p} for p in real.values()] + [{"real": False, **p} for p in synth]


def _load_items() -> List[tuple]:
    with open(ITEM_REGISTRY, encoding="utf-8-sig") as f:
        return [(r["ItemID"], r["ItemType"]) for r in csv.DictReader(f) if r.get("ItemID")]


PERSONAS = _load_personas()
REAL_PERSONAS = [p for p in PERSONAS if p["real"]]
SYNTH_PERSONAS = [p for p in PERSONAS if not p["real"]]
ITEMS = _load_items()


def _m(rng: random.Random, lo: float, hi: float) -> int:
    return int(round(rng.uniform(lo, hi)))


def make_daily_scenario(rng: random.Random) -> Dict[str, Any]:
    """C++ BuildJevDailyQuery 와 같은 {metrics, activities, pools} + persona."""
    persona = rng.choice(REAL_PERSONAS if rng.random() < 0.5 else SYNTH_PERSONAS)
    posture = rng.choices(["stand", "sit", "lie"], [0.7, 0.2, 0.1])[0]
    stand = posture == "stand"
    posture_s = 0.0 if stand else float(_m(rng, 0, 120) if rng.random() < 0.5 else _m(rng, 120, 600))

    actors: List[Dict[str, str]] = []
    player_dist, relation = -1.0, rng.choices(["friendly", "neutral", "hostile"], [0.5, 0.4, 0.1])[0]
    if rng.random() < 0.7:
        player_dist = float(_m(rng, 1, 15))
        if relation != "hostile":  # C++ 도 적대는 풀에서 뺀다
            actors.append({"id": "Player", "desc": f"player|{relation}|{player_dist:.0f}m"})
    # 적 진영은 게임에서 hostile 이라 C++ 가 풀에서 뺀다 → 주변 NPC 는 마을 쪽만.
    others = [p for p in REAL_PERSONAS if p["name"] != persona["name"] and p["name"] not in HOSTILE_NPCS]
    npc_near = 0
    for p in rng.sample(others, k=min(len(others), rng.choices([0, 1, 2, 3], [0.3, 0.35, 0.2, 0.15])[0])):
        d = _m(rng, 1, 15)
        npc_near += d <= 10
        npc_id = "_".join(w.capitalize() for w in p["name"].split("_"))  # commander_vorg → Commander_Vorg
        actors.append({"id": npc_id, "desc": f"npc|{rng.choice(['friendly', 'neutral'])}|{d}m"})
    rng.shuffle(actors)

    places = []
    for fid, ftype in rng.sample(FURNITURE, k=rng.choices([0, 1, 2, 3], [0.25, 0.35, 0.25, 0.15])[0]):
        near = player_dist >= 0 and rng.random() < 0.3
        occ = "occupied" if rng.random() < 0.3 else "vacant"
        places.append({"id": fid, "desc": f"{ftype}|{occ}|{_m(rng, 2, 15)}m" + ("|near_player" if near else "")})

    pois = [
        {"id": f"POI_{n}", "desc": f"poi|{n}|{_m(rng, 3, 40)}m"}
        for n in rng.sample(POI_NAMES, k=rng.choices([0, 1, 2, 3], [0.3, 0.3, 0.25, 0.15])[0])
    ]
    items = [
        {"id": iid, "desc": f"{cat.lower()}|x{rng.randint(1, 5)}"}
        for iid, cat in rng.sample(ITEMS, k=rng.choices([0, 1, 2, 3, 4], [0.2, 0.25, 0.25, 0.15, 0.15])[0])
    ]
    ground = [
        {"id": "%032X" % rng.getrandbits(128), "desc": f"{rng.choice(ITEMS)[0]}|{_m(rng, 1, 15)}m"}
        for _ in range(rng.choices([0, 1, 2], [0.6, 0.3, 0.1])[0])
    ]
    media = [] if posture == "lie" else ["Emote"] if posture == "sit" else ["Dance", "Emote", "Pray", "Sing"]

    # 실행 가능 활동 — C++ BuildJevDailyQuery 의 조건과 동일해야 한다.
    vacant = any("vacant" in p["desc"] for p in places)
    has_cons = any(i["desc"].startswith("consumable") for i in items)
    has_give = any(not i["desc"].startswith("quest") for i in items)
    npc_actors = any(a["desc"].startswith("npc") for a in actors)
    acts = ["stay"]
    acts += ["stand_up"] if not stand else []
    acts += ["look_at"] if posture != "lie" else []
    acts += ["wander"] if stand else []
    acts += ["patrol"] if stand and pois else []
    acts += ["rest"] if stand and vacant else []
    acts += ["emote"] if media else []
    acts += ["use_item"] if has_cons else []
    acts += ["give_item"] if stand and has_give and npc_actors else []
    acts += ["pick_up"] if stand and ground else []

    last = "" if rng.random() < 0.4 else rng.choice(DAILY_ACTS)

    def pct() -> float:  # 대부분 멀쩡, 가끔 지침·부상
        return round(rng.choices([rng.uniform(0.7, 1), rng.uniform(0.3, 0.7), rng.uniform(0.05, 0.3)], [0.72, 0.18, 0.1])[0], 2)

    talked = rng.random() < 0.5
    partners = [a["id"] for a in actors]
    metrics = {
        "posture": posture,
        "posture_s": posture_s,
        "idle_s": float(_m(rng, 10, 60)),
        "player_dist_m": player_dist,
        "player_relation": relation,
        "npc_near": npc_near,
        "last_activity": last,
        "goal": rng.choice(GOALS) if rng.random() < 0.5 else "",
        "hp_pct": pct(),
        "stamina_pct": pct(),
        "hit_s": float(_m(rng, 5, 400)) if rng.random() < 0.2 else -1.0,
        # daily 는 LLM 배치 15s 뒤부터 발동 → 대화 경과 최소 15s.
        "talk_s": float(_m(rng, 15, 400)) if talked else -1.0,
        "talk_with": rng.choice(partners) if talked and partners else "",
    }
    pools = {"actors": actors, "places": places, "pois": pois, "items": items, "ground_items": ground, "media": media}
    return {
        "npc_id": persona["name"],
        "persona": {"role": persona["role"], "traits": persona["traits"]},
        "metrics": metrics,
        "activities": acts,
        "pools": pools,
    }


def make_combat_metrics(rng: random.Random) -> Dict[str, Any]:
    count = rng.choices([1, 2, 3, 4, 5], [0.4, 0.25, 0.15, 0.1, 0.1])[0]
    return {
        "hp_pct": round(rng.uniform(0.05, 1.0), 2),
        "distance_m": round(rng.uniform(0.5, 15.0), 1),
        "enemy_count": count,
        "is_flanked": count >= 2 and rng.random() < 0.5,
    }


# ─────────────────────────────── LLM 라벨 ───────────────────────────────

ACTIVITY_DOC = {
    "stay": "keep doing nothing, stay where you are",
    "stand_up": "stand up from sitting/lying",
    "look_at": "turn to look at someone/something (target 'around' = scan surroundings)",
    "wander": "walk somewhere (dest 'random' = stroll nearby)",
    "patrol": "patrol to a point of interest",
    "rest": "sit on a vacant seat or sleep on a vacant bed",
    "emote": "expressive animation: Emote(gesture) / Pray / Dance / Sing, optionally toward someone ('none' = no one)",
    "use_item": "consume an item from your bag",
    "give_item": "hand an item to a nearby NPC",
    "pick_up": "walk to an item on the ground and pick it up",
}
SLOT_DOC = {
    "target": "who/what",
    "dest": "where to",
    "style": "how (movement style or animation key)",
    "item": "which item",
    "facial": "facial expression",
}


def _situation(sc: Dict[str, Any]) -> List[str]:
    m, p, pools = sc["metrics"], sc["persona"], sc["pools"]

    def ls(key: str) -> str:
        return ", ".join(f"{e['id']} ({e['desc']})" for e in pools.get(key) or []) or "none"

    def band(x: float) -> str:
        return "low" if x < 0.3 else "okay" if x < 0.7 else "fine"

    lines = [
        f"You are {sc['npc_id']}, a {p['role']} (traits: {', '.join(p['traits'])}) in a fantasy village RPG.",
        f"No one is giving you orders right now; you have been idle for {m['idle_s']:.0f}s.",
        "",
        "Situation:",
        f"- posture: {m['posture']}" + (f" for {m['posture_s']:.0f}s" if m["posture"] != "stand" else ""),
        f"- health: {band(m['hp_pct'])}, stamina: {band(m['stamina_pct'])}",
    ]
    if m["hit_s"] >= 0:
        lines.append(f"- you were hit by something {m['hit_s']:.0f}s ago (not in combat now)")
    if m["talk_s"] >= 0:
        who = f" with {m['talk_with']}" if m["talk_with"] else ""
        lines.append(f"- you finished talking{who} {m['talk_s']:.0f}s ago")
    if m["goal"]:
        lines.append(f"- your current aim (from the story): {m['goal']}")
    lines += [
        "- player: "
        + (f"{m['player_dist_m']:.0f}m away, {m['player_relation']}" if m["player_dist_m"] >= 0 else "not in sight"),
        f"- people around: {ls('actors')}",
        f"- furniture: {ls('places')}",
        f"- landmarks: {ls('pois')}",
        f"- your bag: {ls('items')}",
        f"- items on the ground: {ls('ground_items')}",
        f"- your previous activity: {m['last_activity'] or 'none'}",
    ]
    return lines


def _slot_lines(sc: Dict[str, Any], act: str) -> List[str]:
    out = []
    for slot in ACTIVITY_SLOTS[act]:
        cands = slot_candidates(act, slot, sc["pools"])
        if len(cands) <= 1:
            continue  # 선택지 1개 이하는 게임이 채운다
        opts = ", ".join(f"{cid} ({d})" if d and d != cid else cid for cid, d in cands)
        out.append(f"    - {slot} [{SLOT_DOC[slot]}]: {opts}, default")
    return out


def menu_text(sc: Dict[str, Any], acts: Optional[List[str]] = None) -> str:
    """상황 + 활동·슬롯 메뉴 전체. 사람 골드 시트와 활동 가중치 프롬프트가 공유."""
    lines = _situation(sc) + ["", "Available activities and their options ('default' = let the game decide):"]
    for act in acts or sc["activities"]:
        lines.append(f"* {act}: {ACTIVITY_DOC[act]}")
        lines += _slot_lines(sc, act)
    return "\n".join(lines)


def activity_prompt(sc: Dict[str, Any], order: List[str]) -> str:
    # 답 1개를 받으면 최빈 활동(look_at)으로 붕괴한다(스모크 10건 중 8건) → 활동별 가능성 가중치를 받아 샘플링.
    return (
        menu_text(sc, order)
        + "\n\nHow likely is this character to do each activity right now? Think about personality, posture, "
        "who is around and what was done just before. Real villagers vary: they rest, stroll, pray, sing, "
        "chat, look around, or simply stay put.\n"
        'Answer JSON only: {"reason": "<one short sentence>", "weights": {"<activity>": ' + GRADE_HINT + ', ...}} '
        "covering every activity above."
    )


def slot_prompt(sc: Dict[str, Any], act: str) -> str:
    return (
        "\n".join(_situation(sc))
        + f"\n\nYou decided to: {act} — {ACTIVITY_DOC[act]}\nOptions:\n"
        + "\n".join(_slot_lines(sc, act))
        + "\n\nFor each option line, rate how fitting each choice is for this character now. "
        "Pick a facial expression that matches your mood; use default only when nothing fits.\n"
        'Answer JSON only: {"slots": {"<slot>": {"<option id>": ' + GRADE_HINT + ', ...}, ...}}'
    )


def _chat(prompt: str, temperature: float, schema: Any = "json", timeout: float = 120.0) -> Dict[str, Any]:
    body = {
        "model": MODEL,
        "stream": False,
        "think": False,
        # 자유 json 은 가중치를 중첩·전부 1 로 망가뜨린다(실측) → JSON 스키마로 키·정수 범위를 강제.
        "format": schema,
        # format=json 은 끝에 공백을 무한 생성할 때가 있다(단일 호출 400s+ 실측) — 출력 상한 필수.
        "options": {"temperature": temperature, "num_predict": 600},
        "messages": [{"role": "user", "content": prompt}],
    }
    r = httpx.post(OLLAMA, json=body, timeout=timeout)
    r.raise_for_status()
    return json.loads(r.json()["message"]["content"])


def _schema(props: Dict[str, Any]) -> Dict[str, Any]:
    return {"type": "object", "properties": props, "required": list(props)}


# 스키마 정수 필드는 문법 디코딩에서 전부 0 으로 붕괴한다(범위 유무 무관, 실측) → 등급 enum 을 가중치로 환산.
GRADES = {"very_likely": 8.0, "likely": 5.0, "possible": 3.0, "unlikely": 1.0, "never": 0.0}
GRADE_HINT = '"very_likely|likely|possible|unlikely|never"'


def _grade_map(ids: List[str]) -> Dict[str, Any]:
    return _schema({i: {"type": "string", "enum": list(GRADES)} for i in ids})


def _weights(raw: Any, ids: List[str]) -> Dict[str, float]:
    """LLM 등급 dict → 후보 id 만 가중치로. 형식이 틀리면 빈 dict."""
    if not isinstance(raw, dict):
        return {}
    return {str(k).strip(): GRADES[v] for k, v in raw.items() if str(k).strip() in ids and v in GRADES}


def _pick(w: Dict[str, float], rng: random.Random) -> str:
    keys = [k for k, v in w.items() if v > 0]
    return rng.choices(keys, weights=[w[k] for k in keys], k=1)[0]


def label_daily(sc: Dict[str, Any], rng: random.Random, temperature: float) -> Optional[Dict[str, Any]]:
    """활동 가중치 1회 + 슬롯 가중치 1회. 학습 라벨 = 가중치 샘플, 평가 정답 = 가중치 최대(eval)."""
    order = sc["activities"][:]
    rng.shuffle(order)  # 나열 순서 편향 방지
    ans = _chat(activity_prompt(sc, order), temperature, _schema({"reason": {"type": "string"}, "weights": _grade_map(order)}))
    aw = _weights(ans.get("weights"), sc["activities"])
    if not any(v > 0 for v in aw.values()):
        return None
    act = _pick(aw, rng)

    slots: Dict[str, str] = {}
    sw: Dict[str, Dict[str, float]] = {}
    if _slot_lines(sc, act):
        slot_ids = {
            slot: [c for c, _ in slot_candidates(act, slot, sc["pools"])] + [DEFAULT]
            for slot in ACTIVITY_SLOTS[act]
            if len(slot_candidates(act, slot, sc["pools"])) > 1
        }
        schema = _schema({"slots": _schema({k: _grade_map(v) for k, v in slot_ids.items()})})
        raw = _chat(slot_prompt(sc, act), temperature, schema).get("slots") or {}
        for slot in ACTIVITY_SLOTS[act]:
            ids = [cid for cid, _ in slot_candidates(act, slot, sc["pools"])]
            if len(ids) <= 1:
                continue
            w = _weights(raw.get(slot) if isinstance(raw, dict) else None, ids + [DEFAULT])
            sw[slot] = w
            slots[slot] = _pick(w, rng) if any(v > 0 for v in w.values()) else DEFAULT
    return {"activity": act, "slots": slots, "reason": str(ans.get("reason", ""))[:200], "weights": aw, "slot_weights": sw}


def _resume(path: Path) -> int:
    return sum(1 for _ in open(path, encoding="utf-8")) if path.exists() else 0


def gen_daily(count: int, seed: int, temperature: float) -> None:
    out = DATA / "combos_daily.jsonl"
    DATA.mkdir(parents=True, exist_ok=True)
    done = _resume(out)
    i, t0, fails = done, time.time(), 0
    with open(out, "a", encoding="utf-8") as f:
        # 시드 = 시도 번호(실패 건은 건너뜀). 이어쓰기는 마지막 줄의 시도 번호 다음부터.
        last = _read(out)[-1]["id"] if done else f"d{seed}_-1"
        attempt = int(last.rsplit("_", 1)[1]) + 1
        while i < count:
            rng = random.Random(seed * 1_000_003 + attempt)
            attempt += 1
            sc = make_daily_scenario(rng)
            try:
                label = label_daily(sc, rng, temperature)
            except Exception as e:  # 타임아웃·JSON 파손 — 건너뛴다
                label, fails = None, fails + 1
                print(f"[skip] {e}", flush=True)
            if label is None:
                continue
            f.write(json.dumps({"id": f"d{seed}_{attempt - 1}", **sc, "label": label}, ensure_ascii=False) + "\n")
            f.flush()
            i += 1
            if i % 25 == 0:
                rate = (i - done) / (time.time() - t0)
                print(f"{i}/{count} {rate:.2f}/s eta {(count - i) / max(rate, 1e-6) / 60:.0f}min fails={fails}", flush=True)


COMBAT_BATCH = 20


def gen_combat(count: int, seed: int, temperature: float) -> None:
    out = DATA / "combos_combat.jsonl"
    DATA.mkdir(parents=True, exist_ok=True)
    done = _resume(out)
    batch_no = int(_read(out)[-1]["id"].split("_")[1]) + 1 if done else 0  # 이어쓰기: 마지막 배치 다음
    with open(out, "a", encoding="utf-8") as f:
        while done < count:
            rng = random.Random(seed * 1_000_003 + batch_no)
            batch = [make_combat_metrics(rng) for _ in range(COMBAT_BATCH)]
            batch_no += 1
            rows = "\n".join(
                f"{k}. HP {m['hp_pct'] * 100:.0f}%, nearest enemy {m['distance_m']}m, {m['enemy_count']} enemies"
                + (", flanked" if m["is_flanked"] else "")
                for k, m in enumerate(batch, 1)
            )
            prompt = (
                "You control an ordinary melee NPC fighter in a fantasy RPG. For each combat situation choose the "
                "wisest stance: aggressive (press the attack), defensive (block/keep distance, wait for an opening), "
                "flee (disengage and retreat).\n\n"
                f"{rows}\n\n"
                'Answer JSON only: {"1": "<stance>", "2": "<stance>", ...} for every situation number.'
            )
            keys = [str(k) for k in range(1, COMBAT_BATCH + 1)]
            schema = _schema({k: {"type": "string", "enum": list(OPTIONS)} for k in keys})
            try:
                ans = _chat(prompt, temperature, schema)
            except Exception as e:
                print(f"[skip] {e}", flush=True)
                continue
            for k, m in enumerate(batch, 1):
                if ans.get(str(k)) in OPTIONS:
                    cid = f"c{seed}_{batch_no - 1}_{k}"
                    f.write(json.dumps({"id": cid, "metrics": m, "label": ans[str(k)]}) + "\n")
                    done += 1
            f.flush()
            print(f"{done}/{count}", flush=True)


# ─────────────────────────────── 분할·평가 ───────────────────────────────


def _split(cid: str) -> str:
    """상황 단위 결정론 분할 80/10/10 — 한 조합의 패스들이 train·test 에 갈라지지 않는다."""
    h = int(hashlib.md5(cid.encode()).hexdigest(), 16) % 10
    return "test" if h == 0 else "validation" if h == 1 else "train"


def _passes(sc: Dict[str, Any], want: Dict[str, str], weights: Dict[str, Dict[str, float]]) -> List[Dict[str, Any]]:
    """정답 조합 → 패스 단위 예시(run_daily_passes 로 context·옵션을 추론 때와 똑같이 만든다).

    w = 옵션 순서에 맞춘 LLM 가중치(soft·clear 학습과 애매도 판정용), best = 가중치 최대 인덱스들(평가 정답).
    """
    extra: List[tuple] = []

    def choose(slot: str, context: str, texts: List[str], logits: List[float]):
        ids = [t.split("|", 1)[0] for t in texts]
        w = [weights.get(slot, {}).get(i, 0.0) for i in ids]
        best = [k for k, v in enumerate(w) if v == max(w) and v > 0]
        extra.append((max(range(len(logits)), key=logits.__getitem__), best, w))
        return (ids.index(want[slot]) if want.get(slot) in ids else ids.index(DEFAULT)), 1.0

    out = run_daily_passes(sc["metrics"], sc["activities"], sc["pools"], sc["persona"], choose)
    return [
        {"context": c, "options": t, "label": i, "slot": c.split()[1][5:], "heuristic": h, "best": b or [i], "w": w}
        for (c, t, i), (h, b, w) in zip(out["trace"], extra)
    ]


def label_passes(sc: Dict[str, Any]) -> List[Dict[str, Any]]:
    lab = sc["label"]
    return _passes(sc, {"activity": lab["activity"], **lab["slots"]}, {"activity": lab["weights"], **lab["slot_weights"]})


def combat_example(row: Dict[str, Any]) -> Dict[str, Any]:
    probs = heuristic_probs(row["metrics"])
    return {
        "context": build_context(row["metrics"]),
        "options": list(OPTIONS),
        "label": OPTIONS.index(row["label"]),
        "slot": "combat",
        "heuristic": max(range(3), key=probs.__getitem__),
        "best": [OPTIONS.index(row["label"])],
    }


def _read(path: Path) -> List[Dict[str, Any]]:
    return [json.loads(line) for line in open(path, encoding="utf-8")] if path.exists() else []


SOFT_ROWS = 8  # soft 모드: 패스 1개를 가중치 비율대로 이만큼의 줄로 편다(jevlike 는 soft label 미지원)


def is_clear(w: List[float]) -> bool:
    """1위가 very_likely/likely 이고 2위보다 두 등급 이상 앞서면 명확(8 vs ≤3, 5 vs ≤1)."""
    s = sorted(w, reverse=True)
    return s[0] >= 5 and (len(s) < 2 or s[1] <= s[0] - 3)


def train_rows(r: Dict[str, Any], mode: str) -> List[Dict[str, Any]]:
    """평가용 패스 1개 → 학습 줄. soft = 가중치 비율 복제(애매하면 평평하게 배움), clear = 명확한 패스만 argmax."""
    w = r.get("w")
    if not w or sum(w) <= 0:  # 전투(단일 답)·가중치 없음
        return [r] * (SOFT_ROWS if mode == "soft" else 1)
    if mode == "clear":
        return [{**r, "label": w.index(max(w))}] if is_clear(w) else []
    rows = [{**r, "label": k} for k, v in enumerate(w) for _ in range(round(SOFT_ROWS * v / sum(w)))]
    return rows or [{**r, "label": w.index(max(w))}]


def build() -> None:
    """test 는 두 모드 공유(DATA/test.jsonl, 패스당 1줄). train·validation 은 DATA/<mode>/ 에 모드별로."""
    splits: Dict[str, List[Dict[str, Any]]] = {"train": [], "validation": [], "test": []}
    for sc in _read(DATA / "combos_daily.jsonl"):
        splits[_split(sc["id"])].extend(label_passes(sc))
    for row in _read(DATA / "combos_combat.jsonl"):
        splits[_split(row["id"])].append(combat_example(row))

    def write(path: Path, rows: List[Dict[str, Any]]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        random.Random(0).shuffle(rows)
        with open(path, "w", encoding="utf-8") as f:
            for r in rows:  # jevlike 는 context·options·label 만 읽는다. 나머지는 eval 용.
                f.write(json.dumps(r, ensure_ascii=False) + "\n")
        print(path.relative_to(DATA), len(rows))

    write(DATA / "test.jsonl", splits["test"])
    for mode in ("soft", "clear"):
        for name in ("train", "validation"):
            write(DATA / mode / f"{name}.jsonl", [x for r in splits[name] for x in train_rows(r, mode)])


def evaluate(checkpoint: str) -> None:
    svc = JevlikeService(checkpoint_path=checkpoint)
    assert svc.model_loaded, f"체크포인트 로드 실패: {checkpoint}"
    stats: Dict[str, List[int]] = {}  # slot → [n, model_hit, heur_hit]
    for r in _read(DATA / "test.jsonl"):
        probs = svc._model_probs(r["context"], r["options"])
        pred = max(range(len(probs)), key=probs.__getitem__)
        group = r["slot"] if r["slot"] in ("activity", "combat") else "slots"
        for key in (group, r["slot"]) if group == "slots" else (group,):
            s = stats.setdefault(key, [0, 0, 0])
            s[0] += 1
            # 정답 = LLM 가중치 최대(동률이면 어느 것이든). 샘플 라벨은 학습용이라 평가엔 안 쓴다.
            s[1] += pred in r["best"]
            s[2] += r["heuristic"] in r["best"]
    print(f"{'pass':<10}{'n':>6}{'model':>9}{'heur':>9}{'Δ%p':>8}")
    for k, (n, mh, hh) in sorted(stats.items()):
        print(f"{k:<10}{n:>6}{mh / n:>9.1%}{hh / n:>9.1%}{(mh - hh) / n * 100:>8.1f}")

    # 분포 일치도: 모델 분포 vs LLM 가중치 정규화 분포의 총변동거리(TV, 0=같음). 애매한 패스에서 평평하게
    # 퍼지는지(= 다양한 반응)가 목표라 명확/애매를 나눠 본다. top-1 은 애매한 패스에서 원래 의미가 약하다.
    tv: Dict[str, List[float]] = {"clear": [], "ambiguous": []}
    for r in _read(DATA / "test.jsonl"):
        w = r.get("w")
        if not w or sum(w) <= 0:
            continue
        probs = svc._model_probs(r["context"], r["options"])
        target = [v / sum(w) for v in w]
        tv["clear" if is_clear(w) else "ambiguous"].append(0.5 * sum(abs(a - b) for a, b in zip(probs, target)))
    for k, v in tv.items():
        if v:
            print(f"TV {k:<10} n={len(v):>5}  {sum(v) / len(v):.3f}")

    gold(svc)

    # 기준 19: 4패스 지연. emote(style·target·facial) 가 최대 4패스.
    sc = next(s for s in _read(DATA / "combos_daily.jsonl") if s["label"]["activity"] == "emote")
    rng = random.Random(0)
    svc.evaluate_daily(sc["metrics"], ["emote"], sc["pools"], sc["persona"], rng)  # 워밍업
    t0 = time.perf_counter()
    passes = 0
    for _ in range(100):
        passes = max(passes, svc.evaluate_daily(sc["metrics"], ["emote"], sc["pools"], sc["persona"], rng)["passes"])
    print(f"daily 지연 {(time.perf_counter() - t0) * 10:.2f}ms/회 (최대 {passes}패스, 100회 평균)")


def gold(svc: JevlikeService) -> None:
    """사람 골드셋 채점 — gold_answers.json(시트의 JSON 저장) × gold_scenarios.jsonl.

    건너뜀(skip)·미답은 제외. 슬롯은 사람이 default 가 아닌 값을 고른 것만 채점(default = 게임 위임이라 정답 아님).
    llm = 라벨 LLM 의 활동 1위가 사람과 같은 비율 — 합성 라벨 품질의 직접 지표.
    """
    path = DATA / "gold_answers.json"
    if not path.exists():
        print("골드셋: gold_answers.json 없음 — 건너뜀")
        return
    answers = json.loads(path.read_text(encoding="utf-8"))
    stats: Dict[str, List[int]] = {}  # key → [n, model, heur, llm]
    reviewed = flagged = changed = 0  # 검수형 카드(모델 제안을 미리 채운 것)
    for sc in _read(DATA / "gold_scenarios.jsonl"):
        a = answers.get(sc["id"]) or {}
        if a.get("skip") or not a.get("activity"):
            continue
        sug = a.get("suggest")
        if sug:
            reviewed += 1
            flagged += bool(a.get("flag"))
            changed += a["activity"] != sug["activity"]
            # 부자연 표시만 하고 고치지 않은 카드는 사람 정답이 없다 — 일치율에서 뺀다(오류율에만 셈).
            if a.get("flag") and a["activity"] == sug["activity"] and a.get("slots") == sug["slots"]:
                continue
        want = {"activity": a["activity"], **(a.get("slots") or {})}
        for r in _passes(sc, want, {}):
            if r["slot"] != "activity" and want.get(r["slot"], DEFAULT) == DEFAULT:
                continue
            probs = svc._model_probs(r["context"], r["options"])
            key = "activity" if r["slot"] == "activity" else "slots"
            s = stats.setdefault(key, [0, 0, 0, 0])
            s[0] += 1
            s[1] += max(range(len(probs)), key=probs.__getitem__) == r["label"]
            s[2] += r["heuristic"] == r["label"]
            lw = sc.get("llm_weights") or {}
            s[3] += key == "activity" and bool(lw) and max(lw, key=lw.get) == a["activity"]
    for k, (n, mh, hh, lh) in sorted(stats.items()):
        llm = f"  llm {lh / n:.1%}" if k == "activity" else ""
        print(f"골드 {k:<9} n={n:>3}  model {mh / n:.1%}  heur {hh / n:.1%}{llm}")
    if reviewed:
        # 검수형은 모델 제안이 먼저 보여 사람이 동의하기 쉽다(앵커링) → 위 model 일치율은 상향 편향, 이 오류율은 하한값.
        print(f"검수 n={reviewed:>3}  부자연 {flagged / reviewed:.1%}  활동 수정 {changed / reviewed:.1%}  (앵커링: 일치율↑·오류율은 하한)")


def sheet(n: int, seed: int, candidates: int) -> None:
    """사람 정답용 골드셋. 라벨 LLM 이 활동 1위를 명확히 고른 상황만 싣는다(애매하면 사람도 정답을 못 고른다).

    LLM 가중치는 gold_scenarios.jsonl 에만 남기고 시트엔 안 보인다(사람 판단을 끌지 않게).
    """
    rng = random.Random(seed)
    scenarios = []
    for k in range(candidates):
        if len(scenarios) >= n:
            break
        sc = {"id": f"g{seed}_{k}", **make_daily_scenario(rng)}
        if len(sc["activities"]) < 2:
            continue
        order = sc["activities"][:]
        random.Random(k).shuffle(order)
        schema = _schema({"reason": {"type": "string"}, "weights": _grade_map(order)})
        try:
            w = _weights(_chat(activity_prompt(sc, order), 0.0, schema).get("weights"), sc["activities"])
        except Exception as e:
            print(f"[skip] {e}", flush=True)
            continue
        if not is_clear([w.get(x, 0.0) for x in sc["activities"]]):
            continue
        sc["llm_weights"] = w
        scenarios.append(sc)
        print(f"{len(scenarios)}/{n} (후보 {k + 1})", flush=True)
    with open(DATA / "gold_scenarios.jsonl", "w", encoding="utf-8") as g:
        for sc in scenarios:
            g.write(json.dumps(sc, ensure_ascii=False) + "\n")
    _write_sheet(scenarios)


def _write_sheet(scenarios: List[Dict[str, Any]], svc: Optional[JevlikeService] = None) -> None:
    """골드 시트 HTML. svc 를 주면 검수형 — 모델의 최선 선택(argmax)을 카드에 채워 두고 사람은 부자연스러운 것만 표시한다."""
    cards = []
    for sc in scenarios:
        acts = []
        for act in sc["activities"]:
            slots = []
            for slot in ACTIVITY_SLOTS[act]:
                cands = slot_candidates(act, slot, sc["pools"])
                if len(cands) > 1:  # 1개 이하는 게임이 채운다
                    slots.append({"slot": slot, "doc": SLOT_DOC[slot], "options": [[c, d] for c, d in cands]})
            acts.append({"id": act, "doc": ACTIVITY_DOC[act], "slots": slots})
        card = {"id": sc["id"], "situation": _situation(sc), "activities": acts}
        if svc is not None:
            card["suggest"] = _model_pick(svc, sc)
        cards.append(card)
    # 입력은 브라우저 localStorage 자동 저장 → "JSON 저장" 으로 gold_answers.json 을 받아 data/ 에 둔다.
    template = (Path(__file__).parent / "gold_sheet_template.html").read_text(encoding="utf-8")
    path = DATA / "gold_sheet.html"
    path.write_text(template.replace("/*CARDS*/[]", json.dumps(cards, ensure_ascii=False)), encoding="utf-8")
    print(path)


def _model_pick(svc: JevlikeService, sc: Dict[str, Any]) -> Dict[str, Any]:
    """게임과 같은 패스 루프에서 샘플링 대신 argmax — 검수자가 볼 '모델의 최선 선택'. 시트에 안 뜨는 슬롯(후보 ≤1)은 뺀다."""

    def choose(_slot: str, context: str, texts: List[str], logits: List[float]) -> tuple:
        probs = svc._model_probs(context, texts) or _softmax(logits)
        i = max(range(len(probs)), key=probs.__getitem__)
        return i, float(probs[i])

    out = run_daily_passes(sc["metrics"], sc["activities"], sc["pools"], sc.get("persona") or {}, choose)
    shown = {s for s in ACTIVITY_SLOTS[out["activity"]] if len(slot_candidates(out["activity"], s, sc["pools"])) > 1}
    return {"activity": out["activity"], "slots": {k: v for k, v in out["slots"].items() if k in shown}}


def review_sheet() -> None:
    """기존 gold_scenarios.jsonl 로 검수형 시트 재생성 — LLM 재호출 없음. 이미 입력한 답(브라우저 저장분)은 유지된다."""
    _write_sheet(list(_read(DATA / "gold_scenarios.jsonl")), JevlikeService())


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8")  # Windows 콘솔 cp949 는 '—' 등에서 UnicodeEncodeError
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, cnt in (("gen-daily", 2000), ("gen-combat", 2000)):
        p = sub.add_parser(name)
        p.add_argument("--count", type=int, default=cnt)
        p.add_argument("--seed", type=int, default=1)
        p.add_argument("--temperature", type=float, default=0.8)
    sub.add_parser("build")
    p = sub.add_parser("eval")
    p.add_argument("--checkpoint", default=str(ROOT / "app/models/jevlike_tactics.pt"))
    p = sub.add_parser("sheet")
    p.add_argument("--n", type=int, default=50)
    p.add_argument("--candidates", type=int, default=300)
    p.add_argument("--seed", type=int, default=100)  # 99 = 필터 없던 옛 시트(브라우저 저장 키 분리)
    sub.add_parser("review-sheet")
    a = ap.parse_args()
    if a.cmd == "gen-daily":
        gen_daily(a.count, a.seed, a.temperature)
    elif a.cmd == "gen-combat":
        gen_combat(a.count, a.seed, a.temperature)
    elif a.cmd == "build":
        build()
    elif a.cmd == "eval":
        evaluate(a.checkpoint)
    elif a.cmd == "review-sheet":
        review_sheet()
    else:
        sheet(a.n, a.seed, a.candidates)


if __name__ == "__main__":
    main()
