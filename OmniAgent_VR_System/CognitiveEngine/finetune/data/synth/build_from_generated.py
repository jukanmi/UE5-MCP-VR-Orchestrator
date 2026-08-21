# -*- coding: utf-8 -*-
"""generated/scenarios.yaml + speech/*.jsonl → Stage1 학습 JSONL 조립.

speech 는 Sonnet 배치가 작성한다(teacher). 이 스크립트는 병합·검증만 한다:
- 한글 미포함·영문 혼입·길이 이탈·앵무새(플레이어 발화 복창) 행 드롭
- system/user 조립은 `generate_stage1.py` 의 함수를 그대로 재사용(서빙 정합 단일 소스)
- 손작성 시드(scenarios_seed_draft.yaml)는 별도 경로(generate_stage1.py)로 처리한다

사용: python build_from_generated.py [--out ../processed/stage1_e4b_train.jsonl] [--merge-existing PATH]
"""
import argparse
import collections
import json
import os
import re
import sys

import yaml

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

from generate_stage1 import build_natural_context, build_system, is_parrot, load_personas, load_persona_pool  # noqa: E402

GEN_DIR = os.path.join(_HERE, "generated")


def load_speech() -> dict:
    """speech/*.jsonl 병합 → {id: {utterance, speech, tone}}"""
    sdir = os.path.join(GEN_DIR, "speech")
    out = {}
    if not os.path.isdir(sdir):
        return out
    for fname in sorted(os.listdir(sdir)):
        if not fname.endswith(".jsonl"):
            continue
        for line in open(os.path.join(sdir, fname), encoding="utf-8"):
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("id"):
                out[rec["id"]] = rec
    return out


def speech_ok(speech: str, utterance: str) -> str | None:
    """실격 사유를 반환(통과면 None)."""
    if not speech or not speech.strip():
        return "empty"
    s = speech.strip()
    if not (8 <= len(s) <= 140):
        return "length"
    if not re.search(r"[가-힣]", s):
        return "no_hangul"
    if re.search(r"[A-Za-z]{3,}", s):
        return "english"
    if is_parrot(s, utterance):
        return "parrot"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(_HERE, "..", "processed", "stage1_e4b_train_generated.jsonl"))
    ap.add_argument("--report-only", action="store_true")
    args = ap.parse_args()

    scenarios = yaml.safe_load(open(os.path.join(GEN_DIR, "scenarios.yaml"), encoding="utf-8")) or []
    speech = load_speech()
    personas = load_personas()
    pool = load_persona_pool()
    by_name = {p["name"]: p for p in list(personas.values()) + pool}

    rows = []
    drop = collections.Counter()
    seen_speech = collections.Counter()
    for sc in scenarios:
        rec = speech.get(sc["id"])
        if rec is None:
            drop["no_speech_record"] += 1
            continue
        utt = (rec.get("utterance") or sc["utterance"]).strip()
        why = speech_ok(rec.get("speech", ""), utt)
        if why:
            drop[why] += 1
            continue
        persona = by_name.get(sc["npc"])
        if persona is None:
            drop["unknown_persona"] += 1
            continue

        sit = sc["situation"]
        inv = sit.get("inventory") or []
        inventory = ", ".join(f"{i}×1" for i in inv) if inv else "None (empty-handed)"
        seed_view = dict(sc)
        seed_view["utterance"] = utt

        assistant = {
            "mode": sc["gold"]["mode"],
            "facial": sc["gold"]["facial"],
            "speech": rec["speech"].strip(),
            "tone": (rec.get("tone") or "calmly").strip()[:20],
            "actions": sc["gold"]["actions"],
            "plan_achieved": False,
        }
        rows.append({"messages": [
            {"role": "system", "content": build_system(persona, sit["valid_targets"], inventory, sit["sentiment"])},
            {"role": "user", "content": f"Context: {build_natural_context(seed_view)}"},
            {"role": "assistant", "content": json.dumps(assistant, ensure_ascii=False)},
        ]})
        seen_speech[assistant["speech"]] += 1

    ac = collections.Counter()
    for sc in scenarios:
        acts = sc["gold"]["actions"]
        ac[acts[0]["type"] if acts else "(no-action)"] += 1

    print(f"시나리오 {len(scenarios)} · speech 레코드 {len(speech)} → 채택 {len(rows)}행")
    print("드롭:", dict(drop))
    if rows:
        dup = sum(c - 1 for c in seen_speech.values() if c > 1)
        print(f"고유 대사 {len(seen_speech)} / {len(rows)} (중복행 {dup})")
    print("액션 분포(시나리오 기준):", dict(ac.most_common()))

    if args.report_only:
        return
    out_path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as fp:
        for r in rows:
            fp.write(json.dumps(r, ensure_ascii=False) + "\n")
    print(f"→ {out_path}")


if __name__ == "__main__":
    main()
