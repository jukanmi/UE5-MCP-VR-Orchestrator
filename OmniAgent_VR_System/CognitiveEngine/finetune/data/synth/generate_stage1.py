# -*- coding: utf-8 -*-
"""Stage1(e4b) 학습 JSONL 생성 — 서빙 파이프라인 완전 정합.

서빙-학습 정합 원칙 (SPEC_finetune M2):
- system: 프로덕션 `DIALOGUE_STRUCTURED_PROMPT` 를 실제 페르소나 YAML 로 조립 (하드코딩 금지)
- user:   `Context: {natural_context}` — interface_input._build_natural_context 와 동일 꼴
- assistant: `DialogueResponse` 전체 JSON (mode/facial/speech/tone/actions/plan_achieved)
  — actions 배열만 학습하면 speech 생성 붕괴 (재작성 사유 1)
- Rules 게이트: gold actions 를 실제 `rules.validate_and_clamp_action` 에 통과시켜
  살아남은 시드만 채택 (재작성 사유 2)

speech 는 시드에 없음 → teacher(gemma4-12b)가 페르소나 말투로 생성.
희소 액션(Block/Dodge/Dance/Sing/Track) ×3 오버샘플.

사용: python generate_stage1.py [--n 200] [--all] [--seed 42]
"""
import argparse
import collections
import json
import os
import random
import re
import sys
import urllib.request

import yaml

_HERE = os.path.dirname(os.path.abspath(__file__))
_ENGINE_ROOT = os.path.abspath(os.path.join(_HERE, "../../.."))  # CognitiveEngine
sys.path.insert(0, _ENGINE_ROOT)

from app.agents.subgraphs.dialogue import _format_speech_style  # noqa: E402
from app.agents.subgraphs.prompts import DIALOGUE_STRUCTURED_PROMPT  # noqa: E402
from app.agents.subgraphs.rules import validate_and_clamp_action  # noqa: E402
from app.schemas.actions import DIALOGUE_ACTION_FIELD_MAP, GameAction  # noqa: E402

SYNTH_DIR = _HERE
PROCESSED_DIR = os.path.join(_ENGINE_ROOT, "finetune", "data", "processed")
PERSONA_DIR = os.path.join(_ENGINE_ROOT, "app", "agents", "personas", "generic")

OLLAMA = "http://localhost:11434/api/chat"
TEACHER = "gemma4-12b"

RARE_ACTIONS = {"Block", "Dodge", "Dance", "Sing", "Track"}
OVERSAMPLE = 3

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


# ──────────────────────────────────────────────────────────────────────────────
# 페르소나 — 코어 5(서빙 YAML) + 범용 풀(persona_pool.yaml, LIGHT/NPC-v2 각색)
# 혼합 목적: 코어 과적합 방지 — "system 의 페르소나를 따르는 능력" 자체를 학습.
# ──────────────────────────────────────────────────────────────────────────────
CORE_RATIO = 0.3  # 코어:범용 = 30:70 (2026-07-18 사용자 결정)


def load_personas() -> dict:
    personas = {}
    for name in ("elara", "james", "skadi", "moca", "guard"):
        path = os.path.join(PERSONA_DIR, f"{name}.yaml")
        d = yaml.safe_load(open(path, encoding="utf-8"))
        personas[d["name"]] = d
    return personas


def load_persona_pool() -> list:
    path = os.path.join(SYNTH_DIR, "persona_pool.yaml")
    if not os.path.exists(path):
        return []
    return yaml.safe_load(open(path, encoding="utf-8")) or []


def build_system(persona: dict, valid_targets: list, inventory: str) -> str:
    """서빙 _collect_stage1_context 와 동일 조립 — 세션 상태(메모리 등)는 중립 기본값."""
    return DIALOGUE_STRUCTURED_PROMPT.format(
        name=persona["name"],
        role=persona.get("role", ""),
        traits=persona.get("traits", []),
        speech_style=_format_speech_style(persona.get("speech_style")),
        memory="None",
        sentiment="Neutral",
        rag_context="None",
        chat_history="No previous conversation",
        inventory=inventory,
        valid_targets=", ".join(valid_targets) if valid_targets else "Player, Self, Enemy, or an NPC name",
    )


# ──────────────────────────────────────────────────────────────────────────────
# user — interface_input._build_natural_context 동일 꼴
# ──────────────────────────────────────────────────────────────────────────────
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


# ──────────────────────────────────────────────────────────────────────────────
# Rules 게이트 — 서빙 검증기 실통과분만 golden
# ──────────────────────────────────────────────────────────────────────────────
def rules_gate(actions: list, valid_targets: list) -> list | None:
    """gold actions → GameAction 검증. 하나라도 제거되면 None(시드 탈락), 통과 시 클램프 반영본."""
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


# ──────────────────────────────────────────────────────────────────────────────
# teacher — speech/tone 생성
# ──────────────────────────────────────────────────────────────────────────────
def teacher_speech(persona: dict, seed: dict, actions: list, timeout=60) -> dict | None:
    acts = "; ".join(f"{a['type']}({a.get('target') or a.get('item') or a.get('loc') or ''})" for a in actions) or "없음(대화만)"
    hint = seed.get("gold", {}).get("speech_hint", "")
    user = (
        f"NPC: {persona['name']} ({persona.get('role','')})\n"
        f"말투 예시:\n{_format_speech_style(persona.get('speech_style'))}\n"
        f"플레이어 발화: \"{seed.get('utterance','')}\"\n"
        f"수행 액션: {acts}\n"
        + (f"대사 방향: {hint}\n" if hint else "")
        + "이 순간의 NPC 대사를 써라."
    )
    body = json.dumps({
        "model": TEACHER,
        "messages": [{"role": "system", "content": SPEECH_SYSTEM}, {"role": "user", "content": user}],
        "stream": False,
        "format": SPEECH_SCHEMA,
        "think": False,
        "options": {"temperature": 0.6, "num_ctx": 2048, "num_predict": 200},
    }).encode()
    try:
        req = urllib.request.Request(OLLAMA, data=body, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            out = json.loads(json.load(resp)["message"]["content"])
        speech = (out.get("speech") or "").strip()
        # 한글 미포함·영문 혼입·과장 길이 거부
        if not speech or not (5 <= len(speech) <= 120) or not re.search(r"[가-힣]", speech) or re.search(r"[A-Za-z]{3,}", speech):
            return None
        return {"speech": speech, "tone": (out.get("tone") or "").strip()[:20]}
    except Exception:
        return None


# ──────────────────────────────────────────────────────────────────────────────
def load_seeds() -> list:
    """자체 시드 + universal 시드. golden_universal(저품질 [번역] 플레이스홀더)은 제외."""
    seeds = []
    for fname in ("scenarios_seed_draft.yaml", "scenarios_seed.yaml", "universal_scenarios_seed.yaml"):
        path = os.path.join(SYNTH_DIR, fname)
        if not os.path.exists(path):
            continue
        data = yaml.safe_load(open(path, encoding="utf-8")) or []
        seeds.extend(data)
    # id 중복 제거 (scenarios_seed 예시가 draft 와 겹칠 수 있음)
    seen, uniq = set(), []
    for s in seeds:
        if s.get("id") in seen:
            continue
        seen.add(s.get("id"))
        uniq.append(s)
    return uniq


def resolve_persona(seed: dict, personas: dict, pool: list, rng: random.Random) -> dict:
    """30:70 코어:범용 혼합. 풀 비었으면 종전대로 코어만."""
    npc = seed.get("npc", "")
    use_core = (not pool) or (rng.random() < CORE_RATIO)
    if use_core:
        return personas[npc] if npc in personas else personas[rng.choice(list(personas))]
    return rng.choice(pool)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=200, help="teacher 처리 시드 수 (대표 서브셋)")
    ap.add_argument("--all", action="store_true", help="전체 시드 처리")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    personas = load_personas()
    pool = load_persona_pool()
    seeds = load_seeds()
    rng.shuffle(seeds)
    if not args.all:
        seeds = seeds[: args.n]
    print(f"시드 {len(seeds)}개 처리 시작 (teacher={TEACHER}, 범용 풀 {len(pool)}장, 코어비율 {CORE_RATIO})")

    out_rows, action_c = [], collections.Counter()
    n_gate_fail = n_speech_fail = 0
    for i, seed in enumerate(seeds, 1):
        sit = seed.get("situation", {}) or {}
        valid_targets = list(sit.get("valid_targets") or [])
        gold = seed.get("gold", {}) or {}
        actions = gold.get("actions") or []

        gated = rules_gate(actions, valid_targets)
        if gated is None:
            n_gate_fail += 1
            continue

        persona = resolve_persona(seed, personas, pool, rng)
        sp = teacher_speech(persona, seed, gated) or (teacher_speech(persona, seed, gated))
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
        inventory = "None (empty-handed)"
        extra = sit.get("extra", "")
        m = re.search(r"인벤토리에 (\S+)", extra or "")
        if m:
            inventory = f"{m.group(1)}×1"

        row = {"messages": [
            {"role": "system", "content": build_system(persona, valid_targets, inventory)},
            {"role": "user", "content": f"Context: {build_natural_context(seed)}"},
            {"role": "assistant", "content": json.dumps(assistant, ensure_ascii=False)},
        ]}
        copies = 1 + (OVERSAMPLE if any(a["type"] in RARE_ACTIONS for a in gated) else 0)
        for _ in range(copies):
            out_rows.append(row)
            for a in gated:
                action_c[a["type"]] += 1
        if not gated:
            action_c["(no-action)"] += 1

        if i % 20 == 0:
            print(f"  {i}/{len(seeds)} (채택 {len(out_rows)}행, 게이트탈락 {n_gate_fail}, speech실패 {n_speech_fail})", flush=True)

    rng.shuffle(out_rows)
    os.makedirs(PROCESSED_DIR, exist_ok=True)
    out_path = os.path.join(PROCESSED_DIR, "stage1_e4b_train.jsonl")
    with open(out_path, "w", encoding="utf-8") as fp:
        for r in out_rows:
            fp.write(json.dumps(r, ensure_ascii=False) + "\n")

    print(f"완료: {len(out_rows)}행 → {out_path}")
    print(f"Rules 게이트 탈락 {n_gate_fail} · speech 실패 {n_speech_fail}")
    print("액션 분포:", dict(action_c.most_common()))


if __name__ == "__main__":
    main()
