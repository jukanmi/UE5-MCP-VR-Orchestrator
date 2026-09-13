# -*- coding: utf-8 -*-
"""build_dataset_from_raw_lorebooks.py

Reads downloaded raw lorebook files in data/raw:
- data/raw/hf/npc_info.parquet (1,688 NPC character cards & lore)
- data/raw/light/quest_stems/*.json (7,487 LIGHT quest stems & timelines)
- data/raw/hf/multichar.jsonl (Fantasy multi-character dialogue scenes)

Generates universal Stage 1 scenarios and Stage 2 multi-step plans referencing
these real downloaded lorebooks, ensuring high persona & situation diversity.
"""

import os
import glob
import json
import yaml
import pandas as pd
import sys

ce_dir = r"C:\github\UE5_MCP_VR\OmniAgent_VR_System\CognitiveEngine"
if ce_dir not in sys.path:
    sys.path.insert(0, ce_dir)


RAW_DIR = r"C:\github\UE5_MCP_VR\OmniAgent_VR_System\CognitiveEngine\finetune\data\raw"
SYNTH_DIR = r"C:\github\UE5_MCP_VR\OmniAgent_VR_System\CognitiveEngine\finetune\data\synth"


def process_npc_info_parquet():
    """Reads 1,688 character cards from npc_info.parquet to generate Stage 1 scenarios."""
    parquet_path = os.path.join(RAW_DIR, "hf", "npc_info.parquet")
    if not os.path.exists(parquet_path):
        print(f"Skipping npc_info.parquet: File not found at {parquet_path}")
        return []

    df = pd.read_parquet(parquet_path)
    scenarios = []

    for idx, row in df.iterrows():
        c_name = str(row.get("Character Name", f"NPC_{idx}"))
        c_bio = str(row.get("Character Bio", ""))
        c_loc = str(row.get("Location Description", ""))
        sys_prompt = str(row.get("System Prompt", ""))

        # Determine category & intent from bio/loc
        cat = "Social"
        mode = "Social"
        facial = "Neutral"
        actions = []
        # 가구 컨텍스트 기본값(Lifestyle 분기에서 Sit/Sleep 에 맞춰 덮어씀).
        furn_id, furn_type = "Chair_01", "Chair"

        bio_lower = c_bio.lower()
        if any(w in bio_lower for w in ["combat", "bounty", "fighter", "battle", "warrior", "vampire", "hunter"]):
            cat = "Combat"
            mode = "Combat"
            facial = "Angry"
            actions = [{"type": "Attack", "target": "Enemy"}]
        elif any(w in bio_lower for w in ["scholar", "magic", "mystic", "investigate", "secret"]):
            cat = "Investigation"
            mode = "Investigation"
            facial = "Surprised"
            actions = [{"type": "Investigate", "loc": "100,200,0"}]
        elif any(w in bio_lower for w in ["merchant", "trader", "shop", "gold", "coin"]):
            cat = "Task"
            mode = "Task"
            facial = "Happy"
            actions = [{"type": "GiveItem", "target": "Player", "item": "TradeItem"}]
        elif any(w in bio_lower for w in ["sleep", "rest", "inn", "bed", "weary", "drowsy"]):
            # 침대·수면 계열 → Sleep(눕기). 기존엔 이 키워드도 전부 Sit 으로 뭉개
            # Sit 233 : Sleep 0 편향 → 모델이 "누워"도 Sit 을 내던 근원.
            cat = "Lifestyle"
            mode = "Lifestyle"
            facial = "Tired"
            actions = [{"type": "Sleep", "target": "Bed_01"}]
            furn_id, furn_type = "Bed_01", "Bed"
        elif any(w in bio_lower for w in ["tavern", "chair", "seat", "sit"]):
            cat = "Lifestyle"
            mode = "Lifestyle"
            facial = "Neutral"
            actions = [{"type": "Sit", "target": "Chair_01"}]
        else:
            cat = "Social"
            mode = "Social"
            facial = "Happy"
            actions = [{"type": "Comfort", "target": "Player"}]

        scenario = {
            "id": f"raw_npcinfo_{idx + 1:04d}",
            "category": cat,
            "persona": f"{c_name} — {c_bio[:80]}...",
            "location": c_loc[:100],
            "situation": {
                "valid_targets": ["Player", "Self", "Enemy", furn_id],
                "nearby_furniture": [{"id": furn_id, "type": furn_type, "occupied": False, "dist": 2}],
            },
            "utterance": f"Greetings. I am {c_name}. {sys_prompt[-80:] if len(sys_prompt) > 80 else sys_prompt}",
            "gold": {"mode": mode, "facial": facial, "actions": actions},
        }
        scenarios.append(scenario)

    print(f"Processed {len(scenarios)} scenarios from npc_info.parquet")
    return scenarios


def process_light_quest_stems():
    """Reads LIGHT quest stems (7,487 files) to generate Stage 2 multi-step plans."""
    stems_dir = os.path.join(RAW_DIR, "light", "quest_stems")
    json_files = glob.glob(os.path.join(stems_dir, "*.json"))
    if not json_files:
        json_files = glob.glob(os.path.join(stems_dir, "**/*.json"))

    plans = []
    for idx, fpath in enumerate(json_files[:2000]):  # Sample up to 2000 quest stems
        try:
            with open(fpath, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception:
            continue

        qdata = data.get("data", {})
        char = qdata.get("character", "")
        persona = qdata.get("persona", "")
        goal_en = qdata.get("goal", "")
        motivation = qdata.get("mid_motivation") or qdata.get("short_motivation") or ""
        timeline = qdata.get("timeline", [])

        if not char or len(timeline) < 2:
            continue

        steps_ko = []
        for t in timeline[:4]:
            act_str = t.get("action", "")
            if act_str:
                steps_ko.append(f"{act_str} 수행하기")

        if len(steps_ko) < 2:
            continue

        plan = {
            "id": f"raw_lightquest_{idx + 1:04d}",
            "persona": f"{char} ({persona[:60]})",
            "source_goal_en": goal_en,
            "context": f"{char}는 {motivation or goal_en}을(를) 달성하기 위해 행동을 개시한다.",
            "gold": {"goal": goal_en, "steps": steps_ko},
        }
        plans.append(plan)

    print(f"Processed {len(plans)} multi-step plans from LIGHT quest stems")
    return plans


def main():
    os.makedirs(SYNTH_DIR, exist_ok=True)

    scenarios = process_npc_info_parquet()
    plans = process_light_quest_stems()

    out_scenarios_path = os.path.join(SYNTH_DIR, "lorebook_raw_scenarios.yaml")
    out_plans_path = os.path.join(SYNTH_DIR, "lorebook_raw_plans.yaml")

    with open(out_scenarios_path, "w", encoding="utf-8") as f:
        yaml.dump(scenarios, f, allow_unicode=True, sort_keys=False, default_flow_style=False)

    with open(out_plans_path, "w", encoding="utf-8") as f:
        yaml.dump(plans, f, allow_unicode=True, sort_keys=False, default_flow_style=False)

    print(f"\nSaved {len(scenarios)} Stage 1 scenarios -> {out_scenarios_path}")
    print(f"Saved {len(plans)} Stage 2 plans -> {out_plans_path}")


if __name__ == "__main__":
    main()
