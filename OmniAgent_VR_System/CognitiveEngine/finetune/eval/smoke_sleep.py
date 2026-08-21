#!/usr/bin/env python
"""파인튜닝 전/후 "누워"→Sleep 발화 대조 스모크.

grammar 없이 raw 로 호출 — 학습으로 액션 선택이 개선됐는지 판별(few-shot/grammar 크러치 배제).
서빙 venv python 으로 실행(Ollama 로컬). 신모델 gemma4-e4b-dialogue-v1 vs 구 gemma4:e4b.
"""
import json
import re
import urllib.request

SYS = (
    "You are Moca, a ASMR Streamer NPC in a VR game. Respond ONLY as a JSON object with "
    "fields: mode, facial, speech, tone, actions, plan_achieved. actions: list of game actions "
    "you perform RIGHT NOW. A command like 앉아/누워/따라와 tells YOU to do it — YOU perform it. "
    "target MUST be one of: Player, Self, 1001, Bed_01."
)
CASES = [
    '침대에 누워',
    '누워',
    '침대에 누워서 쉬어',
    '거기 침대에 좀 누워봐',
]
CTX_SUFFIX = '. Nearby furniture you can use as Sit/Sleep target: 1001 (Seat, vacant, 2m); Bed_01 (Bed, vacant, 2m)'


def call(model: str, user: str) -> dict:
    body = json.dumps({
        "model": model,
        "messages": [{"role": "system", "content": SYS},
                     {"role": "user", "content": f"Context: Player said: \"{user}\"{CTX_SUFFIX}"}],
        "stream": False,
        "options": {"temperature": 0.3},
    }).encode()
    req = urllib.request.Request("http://127.0.0.1:11434/api/chat", data=body,
                                 headers={"Content-Type": "application/json"})
    raw = json.loads(urllib.request.urlopen(req, timeout=120).read())["message"]["content"]
    m = re.search(r"\{.*\}", raw, re.S)
    try:
        return json.loads(m.group(0)) if m else {"_raw": raw[:120]}
    except Exception:
        return {"_raw": raw[:120]}


def acts(d: dict) -> str:
    out = []
    for a in (d.get("actions") or []):
        if isinstance(a, dict):
            out.append(f"{a.get('type')}:{a.get('target', '')}")
        else:
            out.append(str(a))
    return ",".join(out) or "(none)"


report = {}
for model in ["gemma4:e4b", "gemma4-e4b-dialogue-v1"]:
    rows = []
    sleep_hits = 0
    for u in CASES:
        d = call(model, u)
        a = acts(d)
        if "Sleep" in a:
            sleep_hits += 1
        rows.append({"utterance": u, "actions": a, "full": d})
    report[model] = {"sleep_hits": f"{sleep_hits}/{len(CASES)}", "rows": rows}

import os
out = os.path.join(os.path.dirname(__file__), "smoke_result.json")
with open(out, "w", encoding="utf-8") as fp:
    json.dump(report, fp, ensure_ascii=False, indent=2)
print("RESULT_WRITTEN", out)
