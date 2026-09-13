# -*- coding: utf-8 -*-
"""generated/batches/*.json → speech/*.jsonl 을 로컬 teacher(Ollama)로 채운다.

Sonnet 서브에이전트 경로(SPEECH_TASK.md)의 대체·보완용. 산출 포맷은 동일하므로
두 경로를 배치 단위로 섞어 써도 `build_from_generated.py` 가 그대로 병합한다.

- 이미 존재하는 speech/batch_NNN.jsonl 은 건너뛴다(재개 가능).
- 항목 단위 실패는 건너뛰고 계속 — 병합기가 누락분을 리포트한다.

사용: python fill_speech_ollama.py [--only 0,1,2] [--model gemma4-12b] [--workers 2]
"""
import argparse
import json
import os
import re
import urllib.request
from concurrent.futures import ThreadPoolExecutor

_HERE = os.path.dirname(os.path.abspath(__file__))
GEN_DIR = os.path.join(_HERE, "generated")
OLLAMA = "http://localhost:11434/api/chat"

SPEECH_SCHEMA = {
    "type": "object",
    "properties": {
        "utterance": {"type": "string", "description": "플레이어 발화의 자연스러운 변주 (뜻 유지)"},
        "speech": {"type": "string", "description": "NPC 대사 1~3문장, 한국어 구어"},
        "tone": {"type": "string", "description": "감정 톤 한 단어 영어 (예: calmly)"},
    },
    "required": ["utterance", "speech", "tone"],
}

SYSTEM = """중세 판타지 VR 게임 NPC 대사 작가다.
페르소나·플레이어 발화·NPC 가 지금 수행할 액션·호감도를 받아, 그 순간 NPC 가 말할 대사를 쓴다.

규칙:
- 페르소나 말투 예시의 어투를 그대로 따른다 (존댓말/반말·문장 길이·어미)
- 액션과 모순되지 않게. 액션이 '없음(대화만)'이면 행동 예고 금지
- 호감도가 Hostile 이어도 액션은 수행한다. 태도만 퉁명스럽게. Friendly 면 흔쾌히
- 플레이어 발화를 그대로 되뇌지 마라. NPC 자신의 관점·이유가 한 조각은 들어가야 한다
- 자연스러운 한국어 구어만. 영어 단어·괄호 지문·이모지·추상적 미사여구 금지
- utterance 는 입력 발화의 변주 (뜻·유도되는 액션 불변). 어색하면 원문 그대로 써라
- tone 은 영어 부사 한 단어"""


def build_user(item: dict) -> str:
    p = item["persona"]
    style = "\n".join(f"- {s}" for s in p.get("speech_style", []))
    neg = item.get("negative_kind") or ""
    neg_hint = {
        "refuse_item": "\n특칙: 플레이어가 달라는 물건을 NPC 는 갖고 있지 않다. 없다고 말할 것.",
        "refuse_invalid": "\n특칙: 대상이 없거나 불가능한 요구다. 왜 못 하는지 짧게 말하며 거절할 것.",
        "smalltalk": "\n특칙: 순수 잡담이다. 행동 예고 금지.",
        "question": "\n특칙: 질문에 아는 범위에서 답한다. 행동 예고 금지.",
        "emotion_only": "\n특칙: 플레이어의 감정 표현에 공감하거나 반응한다. 행동 예고 금지. NPC 자신의 솔직한 감정 반응을 한두 문장으로.",
        "request_comfort": "\n특칙: 플레이어가 위로를 요청하는 상황이다. 행동 없이 NPC 가 따뜻하게 말로만 위로한다. 억지로 다가가지 않고 진심을 담아.",
        "ambient_observation": "\n특칙: 플레이어가 주변 상황을 관찰·언급한다. NPC 도 같이 그 상황에 대해 짧게 반응한다. 행동 예고 금지.",
    }.get(neg, "")
    return (
        f"NPC: {p['name']} ({p.get('role','')})\n"
        f"성격: {', '.join(p.get('traits', []))}\n"
        f"말투 예시:\n{style}\n"
        f"소지품: {', '.join(item.get('inventory') or []) or '없음'}\n"
        f"상황: {item.get('situation') or '특이사항 없음'}\n"
        f"플레이어에 대한 호감도: {item.get('sentiment','')}\n"
        f"플레이어 발화: \"{item['utterance']}\"\n"
        f"지금 수행할 액션: {item['action']}"
        f"{neg_hint}\n"
        "이 순간의 NPC 대사를 써라."
    )


def call(model: str, item: dict, timeout: int) -> dict | None:
    messages = [{"role": "system", "content": SYSTEM}, {"role": "user", "content": build_user(item)}]
    for temperature in (0.8, 1.0):
        body = json.dumps({
            "model": model, "messages": messages, "stream": False,
            "format": SPEECH_SCHEMA, "think": False,
            "options": {"temperature": temperature, "num_ctx": 2048, "num_predict": 220},
        }).encode()
        try:
            req = urllib.request.Request(OLLAMA, data=body, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                out = json.loads(json.load(resp)["message"]["content"])
        except Exception:
            continue
        speech = (out.get("speech") or "").strip()
        if not speech or not (8 <= len(speech) <= 140):
            continue
        if not re.search(r"[가-힣]", speech) or re.search(r"[A-Za-z]{3,}", speech):
            continue
        utt = (out.get("utterance") or "").strip() or item["utterance"]
        if not re.search(r"[가-힣]", utt):
            utt = item["utterance"]
        return {"id": item["id"], "utterance": utt, "speech": speech,
                "tone": (out.get("tone") or "calmly").strip()[:20]}
    return None


def process_batch(path: str, model: str, timeout: int, workers: int) -> tuple[int, int]:
    name = os.path.basename(path).replace(".json", ".jsonl")
    out_path = os.path.join(GEN_DIR, "speech", name)
    if os.path.exists(out_path):
        return 0, 0
    items = json.load(open(path, encoding="utf-8"))
    with ThreadPoolExecutor(max_workers=workers) as ex:
        results = list(ex.map(lambda it: call(model, it, timeout), items))
    ok = [r for r in results if r]
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as fp:
        for r in ok:
            fp.write(json.dumps(r, ensure_ascii=False) + "\n")
    return len(ok), len(items) - len(ok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="gemma4-e4b-dialogue-v2")
    ap.add_argument("--only", default="", help="배치 번호 쉼표 목록 (미지정 시 전체)")
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--timeout", type=int, default=90)
    args = ap.parse_args()

    bdir = os.path.join(GEN_DIR, "batches")
    names = sorted(f for f in os.listdir(bdir) if f.endswith(".json"))
    if args.only:
        want = {int(x) for x in args.only.split(",") if x.strip()}
        names = [n for n in names if int(n.split("_")[1].split(".")[0]) in want]

    total_ok = total_fail = 0
    for n in names:
        ok, fail = process_batch(os.path.join(bdir, n), args.model, args.timeout, args.workers)
        total_ok += ok
        total_fail += fail
        print(f"{n}: 성공 {ok} 실패 {fail}" if (ok or fail) else f"{n}: 스킵(기존 출력 존재)", flush=True)
    print(f"\n합계 성공 {total_ok} · 실패 {total_fail}")


if __name__ == "__main__":
    main()
