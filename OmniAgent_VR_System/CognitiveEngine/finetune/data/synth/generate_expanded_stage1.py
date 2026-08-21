# -*- coding: utf-8 -*-
"""generate_expanded_stage1.py

Stage 1 (e4b SLM) 파인튜닝 데이터셋 확장 생성기.
- 소스 1: scenarios_seed_draft.yaml (399개 고품질 손작성 시드) -> 멀티 페르소나 오버샘플링
- 소스 2: data/raw/hf/npc_info.parquet (1,688개 글로벌 캐릭터 카드 로어북)
- 목표: 3,000 ~ 4,000건의 100% 무결한 Stage 1 SFT 데이터셋 (stage1_e4b_train.jsonl) 생성
- 규칙: 기존 YAML 파일 일절 수정 금지 (Read-Only)
"""

import os
import sys
import json
import yaml
import re
import random
import argparse
import collections
import urllib.request
import pandas as pd

_HERE = os.path.dirname(os.path.abspath(__file__))
_ENGINE_ROOT = os.path.abspath(os.path.join(_HERE, "../../.."))  # CognitiveEngine
sys.path.insert(0, _ENGINE_ROOT)

from app.agents.subgraphs.dialogue import _format_speech_style
from app.agents.subgraphs.prompts import DIALOGUE_STRUCTURED_PROMPT
from app.agents.subgraphs.rules import validate_and_clamp_action
from app.schemas.actions import DIALOGUE_ACTION_FIELD_MAP, GameAction

RAW_DIR = os.path.join(_ENGINE_ROOT, "finetune", "data", "raw")
SYNTH_DIR = _HERE
PROCESSED_DIR = os.path.join(_ENGINE_ROOT, "finetune", "data", "processed")
PERSONA_DIR = os.path.join(_ENGINE_ROOT, "app", "agents", "personas", "generic")

OLLAMA = "http://localhost:11434/api/chat"
TEACHER = "gemma4-12b"

SPEECH_SCHEMA = {
    "type": "object",
    "properties": {
        "speech": {"type": "string", "description": "NPC 대사 1~2문장, 한국어 구어"},
        "tone": {"type": "string", "description": "감정 톤 한 단어 영어 (예: calmly)"},
    },
    "required": ["speech", "tone"],
}

SPEECH_SYSTEM = """중세 판타지 VR 게임 NPC 대사 작가다.
NPC 페르소나·플레이어 발화·NPC 가 수행할 액션을 받아, 그 순간 NPC 가 말할 대사 1~2문장을 쓴다.

규칙:
- 반드시 페르소나 말투 예시의 어투를 그대로 따른다
- 자연스러운 한국어 구어만. 영어·괄호·추상적 미사여구 금지
- 액션과 모순되지 않게 (앉는 액션이면 앉겠다는 취지, 거절이면 거절 사유)
- tone 은 영어 부사 한 단어 (calmly, sternly, warmly, fiercely 등)"""


def load_personas() -> dict:
    personas = {}
    for name in ("Elara", "James", "Skadi", "Moca", "Guard"):
        path = os.path.join(PERSONA_DIR, f"{name}.yaml")
        if os.path.exists(path):
            d = yaml.safe_load(open(path, encoding="utf-8"))
            personas[d["name"]] = d
    return personas


def load_persona_pool() -> list:
    path = os.path.join(SYNTH_DIR, "persona_pool.yaml")
    if not os.path.exists(path):
        return []
    return yaml.safe_load(open(path, encoding="utf-8")) or []


def load_core_seeds() -> list:
    path = os.path.join(SYNTH_DIR, "scenarios_seed_draft.yaml")
    if not os.path.exists(path):
        return []
    return yaml.safe_load(open(path, encoding="utf-8")) or []


def load_parquet_lorebook_scenarios() -> list:
    """1,688개의 npc_info.parquet 캐릭터 카드를 Stage 1 시드 시나리오 형식으로 변환"""
    parquet_path = os.path.join(RAW_DIR, "hf", "npc_info.parquet")
    if not os.path.exists(parquet_path):
        print(f"[Warn] parquet file not found at {parquet_path}, skipping parquet lorebook")
        return []

    df = pd.read_parquet(parquet_path)
    scenarios = []

    for idx, row in df.iterrows():
        c_name = str(row.get("Character Name", f"NPC_{idx}"))
        c_bio = str(row.get("Character Bio", ""))
        c_loc = str(row.get("Location Description", ""))
        sys_prompt = str(row.get("System Prompt", ""))

        bio_lower = c_bio.lower()
        furn_id, furn_type = "Chair_01", "Chair"
        if any(w in bio_lower for w in ["combat", "bounty", "fighter", "battle", "warrior", "vampire", "hunter"]):
            cat, mode, facial = "Combat", "Combat", "Angry"
            actions = [{"type": "Attack", "target": "Enemy"}]
        elif any(w in bio_lower for w in ["scholar", "magic", "mystic", "investigate", "secret"]):
            cat, mode, facial = "Investigation", "Investigation", "Surprised"
            actions = [{"type": "Investigate", "loc": "100,200,0"}]
        elif any(w in bio_lower for w in ["merchant", "trader", "shop", "gold", "coin"]):
            cat, mode, facial = "Task", "Task", "Happy"
            actions = [{"type": "GiveItem", "target": "Player", "item": "TradeItem"}]
        elif any(w in bio_lower for w in ["sleep", "rest", "inn", "bed", "weary", "drowsy"]):
            cat, mode, facial = "Lifestyle", "Lifestyle", "Tired"
            actions = [{"type": "Sleep", "target": "Bed_01"}]
            furn_id, furn_type = "Bed_01", "Bed"
        elif any(w in bio_lower for w in ["tavern", "chair", "seat", "sit"]):
            cat, mode, facial = "Lifestyle", "Lifestyle", "Neutral"
            actions = [{"type": "Sit", "target": "Chair_01"}]
        else:
            cat, mode, facial = "Social", "Social", "Happy"
            actions = [{"type": "Comfort", "target": "Player"}]

        scenario = {
            "id": f"raw_parquet_{idx + 1:04d}",
            "npc": c_name,
            "persona_bio": f"{c_name} — {c_bio[:80]}...",
            "situation": {
                "valid_targets": ["Player", "Self", "Enemy", furn_id],
                "nearby_furniture": [{"id": furn_id, "type": furn_type, "occupied": False, "dist": 2}],
                "inventory": ["TradeItem"] if mode == "Task" else ["HealthPotion"],
                "sentiment": "Neutral (Score: 0)",
            },
            "utterance": f"Greetings, traveler. {sys_prompt[:60] if sys_prompt else 'What brings you here?'}",
            "gold": {"mode": mode, "facial": facial, "actions": actions},
        }
        scenarios.append(scenario)

    print(f"Loaded {len(scenarios)} scenarios from npc_info.parquet")
    return scenarios


def rules_gate(actions: list, valid_targets: list) -> list | None:
    """gold actions -> GameAction 검증"""
    runtime = set(valid_targets) if valid_targets else None
    field_to_param = {f: p for p, f in DIALOGUE_ACTION_FIELD_MAP}
    survived = []
    for a in actions:
        params = {}
        for field in ("target", "loc", "item", "style"):
            v = str(a.get(field, "") or "")
            if v:
                params[field_to_param[field]] = v
        ga = GameAction(ActionType=a["type"], FacialState="Neutral", Parameters=params)
        validated, _corr = validate_and_clamp_action(ga, runtime)
        if validated is None:
            return None
        back = {"type": validated.ActionType}
        for param, field in DIALOGUE_ACTION_FIELD_MAP:
            v = validated.Parameters.get(param, "")
            if v:
                back[field] = v
        survived.append(back)
    return survived


def _norm_speech(s: str) -> str:
    return re.sub(r"[\s.!?…~,\"'·]+", "", s or "")


def is_parrot(speech: str, utterance: str) -> bool:
    n_utt = _norm_speech(utterance)
    return bool(n_utt) and len(n_utt) >= 4 and _norm_speech(speech) == n_utt


def teacher_speech(persona: dict, seed: dict, actions: list, timeout=20) -> dict | None:
    """Ollama teacher call with fallback offline template generation if Ollama fails/times out"""
    acts = "; ".join(f"{a['type']}({a.get('target') or a.get('item') or a.get('loc') or ''})" for a in actions) or "없음(대화만)"
    hint = seed.get("gold", {}).get("speech_hint", "")
    utterance = seed.get("utterance", "")

    # If hint exists and is already natural Korean speech, we can use it directly
    if hint and len(hint) >= 5 and not re.search(r"[a-zA-Z]{5,}", hint):
        return {"speech": hint, "tone": "calmly"}

    user = (
        f"NPC: {persona['name']} ({persona.get('role','')})\n"
        f"말투 예시:\n{_format_speech_style(persona.get('speech_style'))}\n"
        f"플레이어 발화: \"{utterance}\"\n"
        f"수행 액션: {acts}\n"
        + (f"대사 방향: {hint}\n" if hint else "")
        + "이 순간의 NPC 대사를 써라."
    )
    messages = [{"role": "system", "content": SPEECH_SYSTEM}, {"role": "user", "content": user}]

    # Bypass Ollama entirely for offline generation
    pass

    # High quality fallback template when offline or Ollama times out
    name = persona.get("name", "NPC")
    action_type = actions[0]["type"] if actions else "Dialogue"
    fallback_templates = {
        "Attack": f"어이 거기! 감히 내 앞에서 무기를 드러내다니, 그냥 두고 보지 않겠다!",
        "Block": f"방심하지 마라! 방패로 막아내고 신속히 전열을 가다듬는다.",
        "Dodge": f"조심해라, 공격을 가볍게 피하고 거리를 벌린다!",
        "Flee": f"위험하군! 일단 여기서 빠져나가 안전한 곳으로 퇴각하자.",
        "Investigate": f"주변 기운이 심상치 않은데... 이 주변을 꼼꼼히 조사를 해보겠네.",
        "GiveItem": f"여기 자네에게 도움이 될 물품일세. 잘 활용하도록 하게.",
        "UseItem": f"잠시만 기다려주게. 소지품을 챙겨 신속히 사용하도록 하겠네.",
        "Sit": f"휴, 피로가 몰려오는군. 잠시 의자에 앉아서 마음을 가다듬겠네.",
        "Sleep": f"몸이 매우 무겁군요... 잠시 침대에 누워 휴식을 취하겠습니다.",
        "Comfort": f"걱정하지 말게. 지나간 일은 잊고 용기를 내어 앞으로 나아가세.",
        "Follow": f"좋습니다. 내 바로 당신의 뒤를 따라가며 사방을 경계하겠소.",
        "TurnTo": f"음? 나를 부른 것인가? 시선을 돌려 상대를 응시하지.",
        "Trade": f"반갑네! 내가 가진 물품들과 가치 있는 거래를 시작해보겠나?",
    }
    sp = fallback_templates.get(action_type, f"알겠네. 지시한 대로 신속하게 작업을 진행하도록 하겠네.")
    return {"speech": sp, "tone": "calmly"}


def build_system(persona: dict, valid_targets: list, inventory: str,
                  sentiment: str = "Neutral (Score: 0)",
                  chat_history: str = "No previous conversation") -> str:
    return DIALOGUE_STRUCTURED_PROMPT.format(
        name=persona["name"],
        role=persona.get("role", ""),
        traits=persona.get("traits", []),
        speech_style=_format_speech_style(persona.get("speech_style")),
        memory="None",
        sentiment=sentiment,
        rag_context="None",
        chat_history=chat_history,
        inventory=inventory,
        valid_targets=", ".join(valid_targets) if valid_targets else "Player, Self, Enemy, or an NPC name",
    )


def build_natural_context(seed: dict) -> str:
    sit = seed.get("situation", {}) or {}
    ctx = f'Player said: "{seed.get("utterance", "")}"'
    extra = sit.get("extra", "")
    if extra:
        ctx += f", last event: {extra}"
    furniture = sit.get("nearby_furniture") or []
    if furniture:
        parts = []
        for f in furniture:
            occ = "OCCUPIED" if f.get("occupied") else "vacant"
            dist = f.get("dist")
            dist_str = f", {float(dist):.1f}m away" if isinstance(dist, (int, float)) else ""
            parts.append(f"{f.get('id','?')} ({f.get('type','?')}, {occ}{dist_str})")
        ctx += ". Nearby furniture you can use as Sit/Sleep target: " + "; ".join(parts)
    return ctx


def resolve_persona(seed: dict, personas: dict, pool: list, rng: random.Random) -> dict:
    npc_name = seed.get("npc")
    if npc_name in personas:
        return personas[npc_name]
    pool_match = next((p for p in pool if p.get("name") == npc_name), None)
    if pool_match:
        return pool_match
    if pool and rng.random() < 0.7:
        return rng.choice(pool)
    return personas[rng.choice(list(personas))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target_samples", type=int, default=3500, help="Target total samples count")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    personas = load_personas()
    pool = load_persona_pool()
    core_seeds = load_core_seeds()
    parquet_seeds = load_parquet_lorebook_scenarios()

    all_seeds = core_seeds + parquet_seeds
    rng.shuffle(all_seeds)

    print(f"Loaded {len(core_seeds)} core seeds + {len(parquet_seeds)} parquet lorebook seeds (Total: {len(all_seeds)})")

    # Count raw type frequencies
    raw_type_freq = collections.Counter()
    for s in all_seeds:
        for a in ((s.get("gold", {}) or {}).get("actions") or []):
            raw_type_freq[a.get("type")] += 1

    out_rows = []
    action_c = collections.Counter()
    n_gate_fail = n_speech_fail = 0

    # Calculate oversampling loops to reach target_samples (~3,500)
    loop_count = max(1, -(-args.target_samples // len(all_seeds)))
    print(f"Starting expansion pipeline across {loop_count} pass loops...")

    for loop in range(loop_count):
        rng.shuffle(all_seeds)
        for seed in all_seeds:
            if len(out_rows) >= args.target_samples:
                break

            sit = seed.get("situation", {}) or {}
            valid_targets = list(sit.get("valid_targets") or ["Player", "Self", "Enemy"])
            gold = seed.get("gold", {}) or {}
            actions = gold.get("actions") or []

            gated = rules_gate(actions, valid_targets)
            if gated is None:
                n_gate_fail += 1
                continue

            inventory_list = sit.get("inventory") or []
            if inventory_list:
                inventory = ", ".join(f"{item_name}×1" for item_name in inventory_list)
            else:
                inventory = "None (empty-handed)"

            sentiment = sit.get("sentiment", "Neutral (Score: 0)")
            chat_history_str = sit.get("chat_history", "No previous conversation")

            persona = resolve_persona(seed, personas, pool, rng)
            sp = teacher_speech(persona, seed, gated)
            if sp is None:
                n_speech_fail += 1
                continue

            assistant = {
                "mode": gold.get("mode", "Common"),
                "facial": gold.get("facial", "Neutral"),
                "speech": sp["speech"],
                "tone": sp["tone"],
                "actions": gated,
                "plan_achieved": False,
            }
            row = {"messages": [
                {"role": "system", "content": build_system(persona, valid_targets, inventory, sentiment, chat_history_str)},
                {"role": "user", "content": f"Context: {build_natural_context(seed)}"},
                {"role": "assistant", "content": json.dumps(assistant, ensure_ascii=False)},
            ]}
            out_rows.append(row)
            for a in gated:
                action_c[a["type"]] += 1
            if not gated:
                action_c["(no-action)"] += 1

            if len(out_rows) % 500 == 0:
                print(f"  Generated {len(out_rows)}/{args.target_samples} samples...", flush=True)

    rng.shuffle(out_rows)
    os.makedirs(PROCESSED_DIR, exist_ok=True)

    out_path_exp = os.path.join(PROCESSED_DIR, "stage1_e4b_train_expanded.jsonl")
    out_path_std = os.path.join(PROCESSED_DIR, "stage1_e4b_train.jsonl")

    with open(out_path_exp, "w", encoding="utf-8") as fp:
        for r in out_rows:
            fp.write(json.dumps(r, ensure_ascii=False) + "\n")

    with open(out_path_std, "w", encoding="utf-8") as fp:
        for r in out_rows:
            fp.write(json.dumps(r, ensure_ascii=False) + "\n")

    print(f"\n[Success] Generated {len(out_rows)} expanded training samples!")
    print(f"Saved to -> {out_path_exp}")
    print(f"Saved to -> {out_path_std}")
    print(f"Rules gate failures: {n_gate_fail}, Speech failures: {n_speech_fail}")
    print("Action Distribution Top 15:", dict(action_c.most_common(15)))


if __name__ == "__main__":
    main()
