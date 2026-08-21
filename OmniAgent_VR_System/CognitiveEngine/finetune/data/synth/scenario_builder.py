# -*- coding: utf-8 -*-
"""Stage1 시나리오 규칙 생성기 — intents/*.yaml × 페르소나 × 상황축 조합.

설계 근거 (2026-08-08):
- 이전 mega 시드(2757개)는 `utterance` 가 비어 있어 서빙(`Player said: "..."`)과
  학습 컨텍스트가 어긋났다. 여기서는 utterance 를 1급 필드로 강제한다.
- 같은 상황 skeleton 을 페르소나 K명으로 렌더 — "system 의 페르소나를 따르는 능력"
  자체를 학습시키기 위함(사용자 지시 2026-08-08). speech 는 페르소나마다 달라야 하므로
  이 단계에선 비워두고 Sonnet 배치가 채운다.
- gold 액션은 `rules.validate_and_clamp_action` 실통과분만 채택(SPEC_finetune M2).

산출:
  generated/scenarios.yaml        — 시나리오 전량(speech 미포함)
  generated/batches/batch_NNN.json — Sonnet speech 배치 입력

사용: python scenario_builder.py [--per-action 30] [--personas 3] [--batch-size 150]
"""
import argparse
import collections
import json
import os
import random
import sys

import yaml

_HERE = os.path.dirname(os.path.abspath(__file__))
_ENGINE_ROOT = os.path.abspath(os.path.join(_HERE, "../../.."))  # CognitiveEngine
sys.path.insert(0, _ENGINE_ROOT)

from app.agents.subgraphs.rules import validate_and_clamp_action  # noqa: E402
from app.schemas.actions import DIALOGUE_ACTION_FIELD_MAP, GameAction  # noqa: E402

INTENTS_DIR = os.path.join(_HERE, "intents")
OUT_DIR = os.path.join(_HERE, "generated")
PERSONA_DIR = os.path.join(_ENGINE_ROOT, "app", "agents", "personas", "generic")

CORE_RATIO = 0.3  # 코어 5 : 범용 풀 = 30:70 (generate_stage1.py 와 동일 기준)

EMOTE_STYLES = ["Wave", "Bow", "Cheer", "Laugh", "Salute", "Threaten"]
MOVE_STYLES = ["Walk", "Run", "Sprint", "Sneak", "Crouch"]

SENTIMENTS = [
    ("Hostile (Score: -80)", 0.2),
    ("Hostile (Score: -40)", 0.1),
    ("Neutral (Score: 0)", 0.4),
    ("Friendly (Score: 40)", 0.15),
    ("Friendly (Score: 80)", 0.15),
]

# 역할 키워드 → 소지품 풀. 페르소나 role/traits 문자열로 매칭.
ROLE_ITEMS = {
    "knight": ["KnightSword", "Shield_Knight", "HealthPotion", "Whetstone"],
    "guard": ["GuardSpear", "GuardShield", "Whistle", "Lantern", "Rations"],
    "pirate": ["Cutlass_Pirate", "RumBottle", "TreasureKey", "Pistol"],
    "sailor": ["NavDagger", "Compass", "StarMap", "Rope"],
    "scholar": ["OldTome", "Spectacles", "InkPot", "Parchment"],
    "mage": ["ManaPotion", "SpellScroll", "RuneStone", "Staff_Apprentice"],
    "merchant": ["CoinPouch", "TradeLedger", "SilkCloth", "SpiceJar"],
    "healer": ["HealthPotion", "Bandage", "HerbBundle", "Salve"],
    "hunter": ["HuntingBow", "Arrow_Bundle", "SkinningKnife", "TrapKit"],
    "innkeeper": ["AleMug", "BreadLoaf", "RoomKey", "Broom"],
    "smith": ["SmithHammer", "IronIngot", "Tongs", "CoalSack"],
    "priest": ["HolySymbol", "CandleStub", "PrayerBook", "IncenseStick"],
    "thief": ["Lockpick", "SmokeBomb", "Dagger_Rusty", "CoinPouch"],
    "farmer": ["Sickle", "SeedBag", "WaterSkin", "StrawHat"],
}
DEFAULT_ITEMS = ["Bread", "WaterSkin", "Torch", "Bandage", "CoinPouch"]

ROLE_KEYWORDS = {
    "knight": ["knight", "commander", "paladin", "기사"],
    "guard": ["guard", "watch", "sentry", "soldier", "경비"],
    "pirate": ["pirate", "corsair", "buccaneer", "해적"],
    "sailor": ["sailor", "navigator", "captain", "선원"],
    "scholar": ["scholar", "sage", "librarian", "researcher", "학자"],
    "mage": ["mage", "wizard", "sorcer", "witch", "mystic", "spirit", "마법"],
    "merchant": ["merchant", "trader", "shop", "vendor", "상인"],
    "healer": ["healer", "medic", "nurse", "apothecar", "치유"],
    "hunter": ["hunter", "ranger", "tracker", "사냥"],
    "innkeeper": ["innkeep", "tavern", "barkeep", "여관"],
    "smith": ["smith", "forge", "blacksmith", "대장"],
    "priest": ["priest", "cleric", "monk", "acolyte", "사제"],
    "thief": ["thief", "rogue", "bandit", "assassin", "도적"],
    "farmer": ["farmer", "peasant", "herder", "농부"],
}


def role_key(persona: dict) -> str:
    blob = " ".join([str(persona.get("role", "")), " ".join(map(str, persona.get("traits", []) or []))]).lower()
    for key, words in ROLE_KEYWORDS.items():
        if any(w in blob for w in words):
            return key
    return "default"


def item_pool(persona: dict) -> list:
    return ROLE_ITEMS.get(role_key(persona), DEFAULT_ITEMS)


def load_personas() -> tuple[list, list]:
    core = []
    for name in ("elara", "james", "skadi", "moca", "guard"):
        path = os.path.join(PERSONA_DIR, f"{name}.yaml")
        if os.path.exists(path):
            core.append(yaml.safe_load(open(path, encoding="utf-8")))
    pool_path = os.path.join(_HERE, "persona_pool.yaml")
    pool = yaml.safe_load(open(pool_path, encoding="utf-8")) if os.path.exists(pool_path) else []
    return core, pool or []


def load_action_specs() -> list:
    specs = []
    for fname in sorted(os.listdir(INTENTS_DIR)):
        if not fname.endswith(".yaml") or fname in ("negative.yaml", "multi.yaml"):
            continue
        data = yaml.safe_load(open(os.path.join(INTENTS_DIR, fname), encoding="utf-8")) or []
        for s in data:
            s["_source"] = fname
            specs.append(s)
    return specs


def load_multi_specs() -> list:
    """복합 액션 spec 로드 (intents/multi.yaml). multi_actions 필드를 가진 항목들."""
    path = os.path.join(INTENTS_DIR, "multi.yaml")
    if not os.path.exists(path):
        return []
    data = yaml.safe_load(open(path, encoding="utf-8")) or []
    for s in data:
        s["_source"] = "multi.yaml"
    return data


def load_negative_specs() -> list:
    path = os.path.join(INTENTS_DIR, "negative.yaml")
    if not os.path.exists(path):
        return []
    return yaml.safe_load(open(path, encoding="utf-8")) or []


def pick_persona(core: list, pool: list, rng: random.Random, used: set) -> dict:
    """코어:범용 = 30:70. skeleton 안에서 이미 쓴 페르소나는 재선택하지 않는다."""
    for _ in range(40):
        src = core if (rng.random() < CORE_RATIO or not pool) else pool
        p = rng.choice(src)
        if p["name"] not in used:
            return p
    return rng.choice(core + pool)


def rand_loc(rng: random.Random) -> str:
    return f"{rng.randint(-900, 900)},{rng.randint(-900, 900)},0"


def build_inventory(persona: dict, rng: random.Random, must_have: str | None = None) -> list:
    pool = item_pool(persona)
    n = rng.randint(2, 4)
    inv = rng.sample(pool, min(n, len(pool)))
    if must_have and must_have not in inv:
        inv[0] = must_have
    return inv


def furniture_block(ftypes: list, rng: random.Random) -> tuple[dict, list]:
    """(타겟 가구, nearby_furniture 목록). 방해 가구(점유·다른 종류)를 섞어 타겟 변별을 학습시킨다."""
    ftype = rng.choice(ftypes)
    target = {"id": f"{ftype}_{rng.randint(1, 4):02d}", "type": ftype,
              "occupied": False, "dist": round(rng.uniform(1.0, 6.0), 1)}
    nearby = [target]
    if rng.random() < 0.55:
        other = rng.choice([t for t in ("Chair", "Bed", "Bench", "Desk", "Stool", "Table") if t != ftype])
        nearby.append({"id": f"{other}_{rng.randint(1, 4):02d}", "type": other,
                       "occupied": rng.random() < 0.5, "dist": round(rng.uniform(1.0, 8.0), 1)})
    rng.shuffle(nearby)
    return target, nearby


def gate(action: dict, valid_targets: list) -> dict | None:
    """rules.validate_and_clamp_action 실통과분만 채택."""
    field_to_param = {f: p for p, f in DIALOGUE_ACTION_FIELD_MAP}
    params = {}
    for field in ("target", "loc", "item", "style"):
        v = str(action.get(field, "") or "")
        if v:
            params[field_to_param[field]] = v
    ga = GameAction(ActionType=action["type"], FacialState="Neutral", Parameters=params)
    validated, _ = validate_and_clamp_action(ga, set(valid_targets))
    if validated is None:
        return None
    back = {"type": validated.ActionType}
    for param, field in DIALOGUE_ACTION_FIELD_MAP:
        v = validated.Parameters.get(param, "")
        if v:
            back[field] = v
    return back


def build_action_scenarios(spec: dict, per_action: int, personas_per: int,
                            core: list, pool: list, rng: random.Random) -> tuple[list, int]:
    out, dropped = [], 0
    intents = spec["intents"]
    for i in range(per_action):
        intent = intents[i % len(intents)]
        sentiment = rng.choices([s for s, _ in SENTIMENTS], weights=[w for _, w in SENTIMENTS])[0]
        dlo, dhi = spec.get("danger", [0.0, 0.2])
        danger = round(rng.uniform(float(dlo), float(dhi)), 2)
        extra = rng.choice(spec.get("extra_events") or [""]) if spec.get("extra_events") else ""

        tk = spec.get("target_kind", "none")
        target, nearby = None, []
        if tk == "furniture":
            fobj, nearby = furniture_block(spec.get("furniture_types") or ["Chair"], rng)
            target = fobj["id"]
        elif tk == "player":
            target = "Player"
        elif tk == "self":
            target = "Self"
        elif tk == "enemy":
            target = "Enemy"
        elif tk == "npc":
            target = rng.choice(["Elara", "James", "Skadi", "Moca", "Guard"])

        loc = rand_loc(rng) if spec.get("loc_kind") == "point" else None
        style = None
        if spec.get("style_kind") == "emote":
            style = rng.choice(EMOTE_STYLES)
        elif spec.get("style_kind") == "move":
            style = rng.choice(MOVE_STYLES)

        used_names: set = set()
        for _k in range(personas_per):
            persona = pick_persona(core, pool, rng, used_names)
            used_names.add(persona["name"])

            item = None
            inv_must = None
            if spec.get("item_kind") == "inventory":
                item = rng.choice(item_pool(persona))
                inv_must = item
            inventory = build_inventory(persona, rng, inv_must)

            valid_targets = ["Player", "Self"]
            if danger >= 0.4 or tk == "enemy":
                valid_targets.append("Enemy")
            if tk == "npc":
                valid_targets.append(target)
            if nearby:
                valid_targets.extend(f["id"] for f in nearby)

            action = {"type": spec["action"]}
            if target:
                action["target"] = target
            if loc:
                action["loc"] = loc
            if item:
                action["item"] = item
            if style:
                action["style"] = style

            gated = gate(action, valid_targets)
            if gated is None:
                dropped += 1
                continue

            sit = {
                "valid_targets": valid_targets,
                "sentiment": sentiment,
                "inventory": inventory,
                "danger": danger,
            }
            if nearby:
                sit["nearby_furniture"] = nearby
            if extra:
                sit["extra"] = extra

            out.append({
                "id": f"gen_{spec['action'].lower()}_{i:03d}_p{_k}",
                "category": spec.get("_source", "").replace(".yaml", ""),
                "npc": persona["name"],
                "situation": sit,
                "utterance": intent,
                "gold": {
                    "mode": spec.get("mode", "Common"),
                    "facial": rng.choice(spec.get("facial") or ["Neutral"]),
                    "actions": [gated],
                },
            })
    return out, dropped


def build_multi_scenarios(specs: list, per_kind: int, personas_per: int,
                          core: list, pool: list, rng: random.Random) -> tuple[list, int]:
    """복합 액션 시나리오 생성.

    spec 의 multi_actions 리스트 각 항목은 단일 액션 딕셔너리.
    모든 액션이 rules 게이트를 통과해야 채택 — 하나라도 탈락하면 드랍.
    """
    out, dropped = [], 0
    for spec in specs:
        intents = spec["intents"]
        multi_action_defs = spec.get("multi_actions", [])
        if not multi_action_defs:
            continue
        for i in range(per_kind):
            intent = intents[i % len(intents)]
            sentiment = rng.choices([s for s, _ in SENTIMENTS], weights=[w for _, w in SENTIMENTS])[0]
            dlo, dhi = spec.get("danger", [0.0, 0.3])
            danger = round(rng.uniform(float(dlo), float(dhi)), 2)
            extra = rng.choice(spec.get("extra_events") or [""]) if spec.get("extra_events") else ""

            # valid_targets 구성: 복합 액션 내 모든 target 포함
            valid_targets = ["Player", "Self"]
            if danger >= 0.4:
                valid_targets.append("Enemy")
            nearby = []
            # furniture target 있으면 nearby_furniture 생성
            for adef in multi_action_defs:
                tk = adef.get("target_kind", "none")
                if tk == "furniture":
                    fobj, nb = furniture_block(adef.get("furniture_types") or ["Chair"], rng)
                    adef["_resolved_target"] = fobj["id"]
                    nearby.extend(nb)
                    valid_targets.extend(f["id"] for f in nb)
                elif tk == "player":
                    adef["_resolved_target"] = "Player"
                elif tk == "self":
                    adef["_resolved_target"] = "Self"
                elif tk == "enemy":
                    adef["_resolved_target"] = "Enemy"
                    if "Enemy" not in valid_targets:
                        valid_targets.append("Enemy")
                elif tk == "npc":
                    npc_target = rng.choice(["Elara", "James", "Skadi", "Moca", "Guard"])
                    adef["_resolved_target"] = npc_target
                    if npc_target not in valid_targets:
                        valid_targets.append(npc_target)
                else:
                    adef["_resolved_target"] = None

            used_names: set = set()
            for _k in range(personas_per):
                persona = pick_persona(core, pool, rng, used_names)
                used_names.add(persona["name"])

                # 인벤토리: 복합 액션 중 item_kind=inventory 항목이 있으면 확보
                inv_must = None
                for adef in multi_action_defs:
                    if adef.get("item_kind") == "inventory":
                        inv_must = rng.choice(item_pool(persona))
                        adef["_resolved_item"] = inv_must
                        break
                inventory = build_inventory(persona, rng, inv_must)

                # 각 액션 게이트 통과 검사
                gated_all = []
                ok = True
                for adef in multi_action_defs:
                    a = {"type": adef["action"]}
                    rt = adef.get("_resolved_target")
                    ri = adef.get("_resolved_item")
                    rl = rand_loc(rng) if adef.get("loc_kind") == "point" else None
                    rs = None
                    if adef.get("style_kind") == "emote":
                        rs = rng.choice(EMOTE_STYLES)
                    elif adef.get("style_kind") == "move":
                        rs = rng.choice(MOVE_STYLES)
                    if rt:
                        a["target"] = rt
                    if ri:
                        a["item"] = ri
                    if rl:
                        a["loc"] = rl
                    if rs:
                        a["style"] = rs
                    g = gate(a, valid_targets)
                    if g is None:
                        ok = False
                        break
                    gated_all.append(g)

                if not ok:
                    dropped += 1
                    continue

                sit: dict = {
                    "valid_targets": valid_targets,
                    "sentiment": sentiment,
                    "inventory": inventory,
                    "danger": danger,
                }
                if nearby:
                    # 중복 제거
                    seen_ids: set = set()
                    dedup = []
                    for f in nearby:
                        if f["id"] not in seen_ids:
                            dedup.append(f)
                            seen_ids.add(f["id"])
                    sit["nearby_furniture"] = dedup
                if extra:
                    sit["extra"] = extra

                out.append({
                    "id": f"gen_multi_{spec['id']}_{i:03d}_p{_k}",
                    "category": "multi",
                    "npc": persona["name"],
                    "situation": sit,
                    "utterance": intent,
                    "gold": {
                        "mode": spec.get("mode", "Common"),
                        "facial": rng.choice(spec.get("facial") or ["Neutral"]),
                        "actions": gated_all,
                    },
                })
    return out, dropped


def build_negative_scenarios(specs: list, per_kind: int, personas_per: int,
                              core: list, pool: list, rng: random.Random) -> list:
    out = []
    for spec in specs:
        intents = spec["intents"]
        for i in range(per_kind):
            intent = intents[i % len(intents)]
            sentiment = rng.choices([s for s, _ in SENTIMENTS], weights=[w for _, w in SENTIMENTS])[0]
            extra = rng.choice(spec.get("extra_events") or [""]) if spec.get("extra_events") else ""
            used_names: set = set()
            for _k in range(personas_per):
                persona = pick_persona(core, pool, rng, used_names)
                used_names.add(persona["name"])
                inventory = build_inventory(persona, rng)
                sit = {
                    "valid_targets": ["Player", "Self"],
                    "sentiment": sentiment,
                    "inventory": inventory,
                    "danger": 0.0,
                }
                if extra:
                    sit["extra"] = extra
                out.append({
                    "id": f"gen_neg_{spec['kind']}_{i:03d}_p{_k}",
                    "category": f"negative/{spec['kind']}",
                    "npc": persona["name"],
                    "situation": sit,
                    "utterance": intent,
                    "gold": {
                        "mode": spec.get("mode", "Common"),
                        "facial": rng.choice(spec.get("facial") or ["Neutral"]),
                        "actions": [],
                        "negative_kind": spec["kind"],
                    },
                })
    return out


def build_hand_scenarios(core: list, pool: list, rng: random.Random, variants: int) -> tuple[list, int]:
    """손작성 시드(scenarios_seed_draft.yaml)를 같은 파이프라인에 편입.

    시드의 speech_hint 는 액션과 어긋난 사례가 다수라 버리고 speech 는 새로 받는다.
    facial 도 시드값(Combat 인데 Happy 등)을 믿지 않고 mode 기준으로 재배정한다."""
    path = os.path.join(_HERE, "scenarios_seed_draft.yaml")
    if not os.path.exists(path):
        return [], 0
    seeds = yaml.safe_load(open(path, encoding="utf-8")) or []
    facial_by_mode = {
        "Combat": ["Angry", "Fear"],
        "Investigation": ["Neutral", "Surprised"],
        "Social": ["Neutral", "Happy"],
        "Task": ["Neutral", "Happy"],
        "Lifestyle": ["Neutral", "Happy", "Tired"],
        "Common": ["Neutral"],
    }
    out, dropped = [], 0
    for seed in seeds:
        utt = (seed.get("utterance") or "").strip()
        if not utt:
            dropped += 1
            continue
        sit = dict(seed.get("situation") or {})
        valid_targets = list(sit.get("valid_targets") or ["Player", "Self"])
        gold = seed.get("gold") or {}
        gated = []
        ok = True
        for a in gold.get("actions") or []:
            g = gate(a, valid_targets)
            if g is None:
                ok = False
                break
            gated.append(g)
        if not ok:
            dropped += 1
            continue

        mode = gold.get("mode", "Common")
        used_names: set = set()
        for k in range(variants):
            if k == 0 and seed.get("npc") in {p["name"] for p in core + pool}:
                persona = next(p for p in core + pool if p["name"] == seed["npc"])
            else:
                persona = pick_persona(core, pool, rng, used_names)
            used_names.add(persona["name"])
            out.append({
                "id": f"hand_{seed['id']}_p{k}",
                "category": f"hand/{seed.get('category', mode)}",
                "npc": persona["name"],
                "situation": sit,
                "utterance": utt,
                "gold": {
                    "mode": mode,
                    "facial": rng.choice(facial_by_mode.get(mode, ["Neutral"])),
                    "actions": gated,
                },
            })
    return out, dropped


def persona_card(persona: dict) -> dict:
    return {
        "name": persona["name"],
        "role": persona.get("role", ""),
        "traits": list(persona.get("traits", []) or []),
        "speech_style": list(persona.get("speech_style", []) or []),
    }


def write_batches(scenarios: list, personas_by_name: dict, batch_size: int, append: bool = False) -> int:
    bdir = os.path.join(OUT_DIR, "batches")
    os.makedirs(bdir, exist_ok=True)
    if append:
        n = len([f for f in os.listdir(bdir) if f.endswith(".json")])
    else:
        for old in os.listdir(bdir):
            os.remove(os.path.join(bdir, old))
        n = 0
    first = n
    for start in range(0, len(scenarios), batch_size):
        chunk = scenarios[start:start + batch_size]
        items = []
        for s in chunk:
            gold = s["gold"]
            acts = gold.get("actions") or []
            act_desc = "없음(대화만)" if not acts else "; ".join(
                f"{a['type']}(" + ", ".join(f"{k}={v}" for k, v in a.items() if k != "type") + ")" for a in acts
            )
            items.append({
                "id": s["id"],
                "persona": persona_card(personas_by_name[s["npc"]]),
                "utterance": s["utterance"],
                "action": act_desc,
                "negative_kind": gold.get("negative_kind", ""),
                "sentiment": s["situation"]["sentiment"],
                "situation": s["situation"].get("extra", ""),
                "inventory": s["situation"].get("inventory", []),
            })
        with open(os.path.join(bdir, f"batch_{n:03d}.json"), "w", encoding="utf-8") as fp:
            json.dump(items, fp, ensure_ascii=False, indent=1)
        n += 1
    return n - first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-action", type=int, default=30, help="액션당 상황 skeleton 수")
    ap.add_argument("--personas", type=int, default=3, help="skeleton 당 페르소나 변형 수")
    ap.add_argument("--neg-per-kind", type=int, default=40)
    ap.add_argument("--batch-size", type=int, default=150)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--hand-only", action="store_true",
                    help="손작성 시드만 처리해 기존 scenarios.yaml·batches 에 append")
    ap.add_argument("--hand-variants", type=int, default=1, help="손작성 시드당 페르소나 변형 수")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    core, pool = load_personas()
    personas_by_name = {p["name"]: p for p in core + pool}

    if args.hand_only:
        hand, dropped = build_hand_scenarios(core, pool, rng, args.hand_variants)
        prev_path = os.path.join(OUT_DIR, "scenarios.yaml")
        prev = yaml.safe_load(open(prev_path, encoding="utf-8")) if os.path.exists(prev_path) else []
        with open(prev_path, "w", encoding="utf-8") as fp:
            yaml.dump((prev or []) + hand, fp, allow_unicode=True, sort_keys=False, width=200)
        n_new = write_batches(hand, personas_by_name, args.batch_size, append=True)
        print(f"손작성 시드 {len(hand)}개 편입(탈락 {dropped}) · 배치 {n_new}개 추가 · 누적 시나리오 {len(prev or []) + len(hand)}")
        return

    specs = load_action_specs()
    neg_specs = load_negative_specs()
    multi_specs = load_multi_specs()
    print(f"액션 스펙 {len(specs)}종 · negative {len(neg_specs)}종 · multi {len(multi_specs)}종 · 페르소나 코어 {len(core)} + 풀 {len(pool)}")

    scenarios, total_dropped = [], 0
    for spec in specs:
        rows, dropped = build_action_scenarios(spec, args.per_action, args.personas, core, pool, rng)
        scenarios.extend(rows)
        total_dropped += dropped
        if dropped:
            print(f"  ! {spec['action']} rules 게이트 탈락 {dropped}건")
    scenarios.extend(build_negative_scenarios(neg_specs, args.neg_per_kind, args.personas, core, pool, rng))
    if multi_specs:
        multi_rows, multi_dropped = build_multi_scenarios(multi_specs, args.per_action, args.personas, core, pool, rng)
        scenarios.extend(multi_rows)
        total_dropped += multi_dropped
        print(f"  multi 시나리오 {len(multi_rows)}개 생성 (탈락 {multi_dropped})")

    rng.shuffle(scenarios)
    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, "scenarios.yaml")
    with open(out_path, "w", encoding="utf-8") as fp:
        yaml.dump(scenarios, fp, allow_unicode=True, sort_keys=False, width=200)

    n_batches = write_batches(scenarios, personas_by_name, args.batch_size)

    ac = collections.Counter()
    for s in scenarios:
        acts = s["gold"]["actions"]
        ac[acts[0]["type"] if acts else "(no-action)"] += 1
    print(f"\n완료: 시나리오 {len(scenarios)}개 → {out_path}")
    print(f"게이트 탈락 {total_dropped} · 배치 {n_batches}개 → {os.path.join(OUT_DIR, 'batches')}")
    print(f"고유 발화 {len({s['utterance'] for s in scenarios})} · 고유 페르소나 {len({s['npc'] for s in scenarios})}")
    print("액션 분포:", dict(ac.most_common()))


if __name__ == "__main__":
    main()
