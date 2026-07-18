# -*- coding: utf-8 -*-
"""LIGHT quest_stems → 우리 Stage2 plan seed 어댑터 (비전투 다양성 주력).

LIGHT 각 퀘스트: character·goal·(short/mid/long)motivation·timeline(동사+인자)·graph.
graph(agents/objects/rooms)로 인자 토큰을 target(인물)/item(사물)/loc(장소)로 분리,
timeline 동사를 EAction 으로 매핑 → Stage2 goal+steps seed.
character 는 직업 키워드로 우리 5 NPC(Elara/James/Skadi/Moca/Guard) 리맵.
goal/steps 영문 유지 — 한국어화는 koreanize_stage2.py(teacher)가 후단 처리.
"""
import json, glob, os, re, collections

# LIGHT timeline 동사 → (EAction, 기본슬롯). 슬롯은 graph 매칭이 우선, 미매칭 시 기본슬롯.
VERB_MAP = {
    "go": ("Move", "loc"), "get": ("PickUp", "item"), "grab": ("PickUp", "item"),
    "follow": ("Follow", "target"), "wear": ("Equip", "item"), "wield": ("Equip", "item"),
    "equip": ("Equip", "item"), "give": ("GiveItem", "item"), "put": ("Drop", "item"),
    "drop": ("Drop", "item"), "eat": ("UseItem", "item"), "drink": ("UseItem", "item"),
    "use": ("UseItem", "item"), "hug": ("Comfort", "target"), "hit": ("Attack", "target"),
    "steal": ("PickUp", "item"), "remove": ("Unequip", "item"), "take": ("PickUp", "item"),
    "look": ("Scan", None), "search": ("Investigate", "loc"), "examine": ("Investigate", "loc"),
    "wait": ("Wait", None), "sit": ("Sit", "target"), "sleep": ("Sleep", "target"),
    "read": ("Read", "target"), "pray": ("Pray", "target"), "hold": ("HandObject", "item"),
}
ACT_CAT = {
    "Move": "Common", "Follow": "Common", "Wait": "Common", "Scan": "Common", "Idle": "Common",
    "UseItem": "Common", "Equip": "Common", "Unequip": "Common",
    "Attack": "Combat", "PickUp": "Task", "Drop": "Task",
    "GiveItem": "Social", "Comfort": "Social", "HandObject": "Social",
    "Investigate": "Investigation", "Sit": "Lifestyle", "Sleep": "Lifestyle",
    "Read": "Lifestyle", "Pray": "Lifestyle",
}
STOP = {"the", "a", "an", "to", "from", "on", "in", "at", "my", "some", "of", "with", "for", "and"}

# character 설명 → 우리 5 NPC 리맵 (직업/역할 키워드, 위→아래 우선)
NPC_RULES = [
    ("Moca", r"merchant|trader|seller|shopkeeper|vendor|owner|peddler|innkeep"),
    ("Guard", r"guard|soldier|knight|watchman|sentry|constable|warden"),
    ("Skadi", r"warrior|fighter|hunter|archer|swordsman|mercenary|pirate|bandit|thief"),
    ("Elara", r"healer|priest|nun|monk|witch|wizard|mage|cleric|apothecary|nurse"),
    ("James", r"farmer|peasant|worker|servant|laborer|miller|fisher|shepherd|cook|smith|carpenter|boy|girl"),
]


def build_vocab(graph_str: str):
    """graph → (agents, objects, rooms) 각 소문자 명칭 집합."""
    try:
        g = json.loads(graph_str)
    except Exception:
        return set(), set(), set()
    agents, objects, rooms = set(), set(), set()
    for nid, node in g.get("nodes", {}).items():
        name = (node.get("name") or nid).lower()
        name = re.sub(r"_\d+$", "", name).strip()
        if node.get("agent"):
            agents.add(name)
        elif "room" in (node.get("classes") or []):
            rooms.add(name)
        elif node.get("object"):
            objects.add(name)
    return agents, objects, rooms


def _match(arg: str, vocab: set):
    """인자 문자열에서 vocab 명칭 부분매칭 — 가장 긴 것."""
    hit = ""
    for name in vocab:
        if name and name in arg and len(name) > len(hit):
            hit = name
    return hit


def parse_action(raw: str, agents: set, objects: set, rooms: set):
    """'give incense customer' → {type: GiveItem, item: incense, target: customer}."""
    toks = re.sub(r"[.,]", "", raw.lower()).split()
    if not toks:
        return None
    verb = toks[0]
    if verb not in VERB_MAP:
        return None
    action, default_slot = VERB_MAP[verb]
    arg = " ".join(t for t in toks[1:] if t not in STOP).strip()
    step = {"type": action}
    if arg:
        # graph 매칭 우선 — agent→target, room→loc, object→item. 겹치면 각 슬롯에 분리.
        t = _match(arg, agents)
        l = _match(arg, rooms)
        i = _match(arg, objects)
        if t:
            step["target"] = t
        if i and i != t:
            step["item"] = i
        if l and action == "Move":
            step["loc"] = l
        # 아무 것도 안 걸리면 기본 슬롯에 통짜 (최대 3단어)
        if not (t or i or (l and action == "Move")) and default_slot:
            step[default_slot] = " ".join(arg.split()[:3])
    return step


def remap_npc(character: str, persona: str) -> str:
    text = f"{character} {persona}".lower()
    for npc, pat in NPC_RULES:
        if re.search(pat, text):
            return npc
    return ""  # 미매칭 — 동물/괴수 등. 필터 대상


def convert(quest: dict):
    d = quest["data"]
    agents, objects, rooms = build_vocab(d.get("graph", "{}"))
    steps, cats = [], collections.Counter()
    for t in d.get("timeline", []):
        s = parse_action(t.get("action", ""), agents, objects, rooms)
        if s:
            steps.append(s)
            cats[ACT_CAT.get(s["type"], "Common")] += 1
    if len(steps) < 2:
        return None
    return {
        "source": "light_quests",
        "character": d.get("character", ""),
        "npc": remap_npc(d.get("character", ""), d.get("persona", "")),
        "persona": d.get("persona", ""),
        "goal_en": d.get("goal", ""),
        "motivation": d.get("mid_motivation", "") or d.get("short_motivation", ""),
        "mode": cats.most_common(1)[0][0],
        "steps": steps,
    }


def main(src_glob, out_path, limit=None):
    files = sorted(glob.glob(src_glob))
    if limit:
        files = files[:limit]
    out, mode_c, act_c, npc_c = [], collections.Counter(), collections.Counter(), collections.Counter()
    slot_sep = 0  # target+item 동시 분리된 스텝 수 (파싱 정제 효과 지표)
    for f in files:
        try:
            q = json.load(open(f, encoding="utf-8"))
        except Exception:
            continue
        rec = convert(q)
        if rec:
            out.append(rec)
            mode_c[rec["mode"]] += 1
            npc_c[rec["npc"] or "(미매칭)"] += 1
            for s in rec["steps"]:
                act_c[s["type"]] += 1
                if "target" in s and "item" in s:
                    slot_sep += 1
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as fp:
        for r in out:
            fp.write(json.dumps(r, ensure_ascii=False) + "\n")
    print(f"변환 {len(out)}개 → {out_path}")
    tot = sum(mode_c.values())
    print("=== 모드 분포 ===")
    for m, c in mode_c.most_common():
        print(f"  {m:14} {c:5} ({100*c/tot:.0f}%)")
    print("=== NPC 리맵 분포 ===")
    for n, c in npc_c.most_common():
        print(f"  {n:10} {c}")
    print(f"target+item 동시 분리 스텝: {slot_sep}")
    print("=== EAction 분포 ===")
    for a, c in act_c.most_common():
        print(f"  {a:10} {c}")


if __name__ == "__main__":
    base = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(base, "../../.."))  # CognitiveEngine/finetune
    main(os.path.join(root, "data/raw/light/quest_stems/*.json"),
         os.path.join(root, "data/processed/stage2_light.jsonl"))
