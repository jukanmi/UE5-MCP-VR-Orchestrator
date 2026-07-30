# -*- coding: utf-8 -*-
"""Stage2(12B plan) 학습 JSONL 생성 — 서빙 파이프라인 완전 정합.

서빙-학습 정합 원칙 (SPEC_finetune M3):
- system: 프로덕션 `PLAN_SYSTEM_PROMPT` 그대로 (하드코딩 재작성 금지)
- user:   서빙 `_generate_plans` 입력 꼴 — `=== NPC: <id> ===` 섹션 +
  `_serialize_dialogue` 텍스트([Mode:][Facial:] "speech"). 시드에는 Stage1 응답이
  없으므로 context 를 상황 발화로 근사 배치
- assistant: `PlanBatchResponse` JSON — {"npcs":[{"npc_id","goal","steps"}]}
  (goal/steps 만 있는 임의 dict 학습 금지 — grammar 스키마와 불일치)

소스: golden_plan_seed.yaml(사람 작성 앵커) + golden_plan_seed_light.yaml(LIGHT 한국어화)
+ universal_plan_seed.yaml. golden_universal_plans([번역] 플레이스홀더 저품질)은 제외.
"""
import json
import os
import re
import sys

import yaml

_HERE = os.path.dirname(os.path.abspath(__file__))
_ENGINE_ROOT = os.path.abspath(os.path.join(_HERE, "../../.."))
sys.path.insert(0, _ENGINE_ROOT)

from app.agents.subgraphs.prompts import PLAN_SYSTEM_PROMPT  # noqa: E402

PROCESSED_DIR = os.path.join(_ENGINE_ROOT, "finetune", "data", "processed")

SOURCES = ("golden_plan_seed.yaml", "golden_plan_seed_light.yaml",
           "golden_plan_seed_core.yaml", "universal_plan_seed.yaml")

# NPC 미지정(universal Mage 등) 시드용 배정 풀 — 범용 페르소나 풀 있으면 그 이름을 사용
# (12B 는 스토리 진행 특화: 임의 npc_id 헤더에 일반화되도록 코어 5 에 안 가둠)
CORE_NPCS = ("Elara", "James", "Skadi", "Moca", "Guard")


def load_npc_pool() -> tuple:
    path = os.path.join(_HERE, "persona_pool.yaml")
    if os.path.exists(path):
        cards = yaml.safe_load(open(path, encoding="utf-8")) or []
        names = tuple(c["name"] for c in cards if c.get("name"))
        if names:
            return CORE_NPCS + names
    return CORE_NPCS


NPC_POOL = CORE_NPCS  # main() 에서 load_npc_pool() 로 대체


def validate_plan(goal: str, steps: list) -> bool:
    """goal 구체성·steps 완결성 최소 검증 (PLAN_SYSTEM_PROMPT 기준)."""
    if not goal or len(goal) > 60 or not re.search(r"[가-힣]", goal):
        return False
    if not (2 <= len(steps) <= 4):
        return False
    for s in steps:
        if not s or len(s.strip()) < 8 or not re.search(r"[가-힣]", s):
            return False
    return True


def build_user(npc: str, context: str) -> str:
    """서빙 입력 근사 — [Mode:][Facial:] 헤더 + 상황을 발화 라인 위치에 배치."""
    ctx = " ".join((context or "").split())
    return f'=== NPC: {npc} ===\n[Mode: Common] [Facial: Neutral]\n"{ctx}"'


def main():
    npc_pool = load_npc_pool()
    generic_npc_pool = [n for n in npc_pool if n not in CORE_NPCS]
    if not generic_npc_pool:
        generic_npc_pool = ["GenericNPC"]
    entries = []
    n_invalid = 0
    pool_i = 0
    for fname in SOURCES:
        path = os.path.join(_HERE, fname)
        if not os.path.exists(path):
            continue
        data = yaml.safe_load(open(path, encoding="utf-8")) or []
        for p in data:
            gold = p.get("gold", {}) or {}
            goal = (gold.get("goal") or "").strip()
            steps = [s.strip() for s in (gold.get("steps") or []) if s and s.strip()]
            if not validate_plan(goal, steps):
                n_invalid += 1
                continue
            npc = p.get("npc") or ""
            if npc not in npc_pool:
                npc = generic_npc_pool[pool_i % len(generic_npc_pool)]
                pool_i += 1
            assistant = {"npcs": [{"npc_id": npc, "goal": goal, "steps": steps}]}
            entries.append({"messages": [
                {"role": "system", "content": PLAN_SYSTEM_PROMPT},
                {"role": "user", "content": build_user(npc, p.get("context", ""))},
                {"role": "assistant", "content": json.dumps(assistant, ensure_ascii=False)},
            ]})

    os.makedirs(PROCESSED_DIR, exist_ok=True)
    out_path = os.path.join(PROCESSED_DIR, "stage2_12b_train.jsonl")
    with open(out_path, "w", encoding="utf-8") as fp:
        for e in entries:
            fp.write(json.dumps(e, ensure_ascii=False) + "\n")
    print(f"완료: {len(entries)}행 (검증 탈락 {n_invalid}) → {out_path}")


if __name__ == "__main__":
    main()
