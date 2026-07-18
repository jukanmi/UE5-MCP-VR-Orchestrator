# -*- coding: utf-8 -*-
"""stage2_light.jsonl → 한국어 Stage2 골든 시드 (teacher: 로컬 Ollama gemma4-12b).

품질 필터(NPC 리맵 성공·스텝 3+·motivation 존재) 통과 레코드를 샘플링,
teacher 가 goal/steps 를 자연스러운 한국어 plan 으로 재작성 —
PLAN_SYSTEM_PROMPT 검수 기준(구체적 goal, 실행 가능한 2~4 steps) 준수.
출력: golden_plan_seed_light.yaml (사람 검수용 초안 — golden_plan_seed.yaml 과 별도 파일).
"""
import json, os, re, random, argparse, urllib.request

OLLAMA = "http://localhost:11434/api/chat"
MODEL = "gemma4-12b"  # llm_factory.py MODELS["gemma4"] 와 동일 태그

SCHEMA = {
    "type": "object",
    "properties": {
        "context_ko": {"type": "string", "description": "재계획 직전 상황 요약 1~2문장, 한국어"},
        "goal_ko": {"type": "string", "description": "구체적 목표, 짧은 한국어 구"},
        "steps_ko": {"type": "array", "items": {"type": "string"}, "minItems": 2, "maxItems": 4},
    },
    "required": ["context_ko", "goal_ko", "steps_ko"],
}

SYSTEM = """중세 판타지 마을 VR 게임의 NPC 행동 계획(plan) 작성자다.
영문 퀘스트(캐릭터·동기·목표·행동 시퀀스)를 받아, 지정된 NPC 가 수행할 한국어 plan 으로 재작성하라.

NPC 페르소나 (반드시 성격에 맞게 각색):
- Elara=기사단장(근엄한 남성 기사단장, 규율·명예·수도 수호), James=항법사(진중 과묵 남성 항법사, 스카디 해적단·항로/별자리),
- Skadi=해적선장(사나운 여성 해적선장, 약탈·호탕·결투), Moca=ASMR 스트리머(차분 다정 여성, 속삭임·휴식·소음 금지), Guard=경비병(규율, 순찰·조사)

규칙:
- 원 퀘스트의 절도·악행이 NPC 성격과 안 맞으면 목적을 각색하라 (예: 훔치기 → 분실물 회수, 조사, 의뢰받은 수거)
- goal_ko: 구체적 결과를 담은 짧은 구. 나쁜 예: "임무 수행", "플레이어 돕기". 좋은 예: "서쪽 문 침입자를 확인하고 마을 안전 확보"
- steps_ko: 2~4개. 각 스텝은 "~로 이동해 ~하기" 처럼 장소·대상·행동이 담긴 완결된 구여야 한다.
  나쁜 예: "동전 획득", "이동". 좋은 예: "시장 좌판에서 은식기를 챙기기", "연회장으로 이동해 귀족들에게 은식기 나눠주기"
- context_ko: 이 plan 이 나오게 된 상황 1~2문장
- 출력에 영어 단어·괄호 병기 금지. 고유명사는 자연스러운 한국어로 현지화 (waiter→여관 종업원, ballroom→연회장)
- 현대 물건은 중세식으로 대체 (사진→초상화, 시계→해시계)
- goal_ko 에 NPC 이름·"계획"·"임무" 단어 금지. 각 스텝은 "~하기" 로 끝나는 완결형

좋은 출력 예시 1:
{"context_ko": "마을 광장에서 축제 준비가 한창인데 은식기가 부족하다는 소식을 들었다.", "goal_ko": "연회에 쓸 은식기를 모아 귀족들에게 나눠주기", "steps_ko": ["창고에서 은식기 상자를 챙기기", "연회장으로 이동해 귀족들에게 은식기 나눠주기", "남은 식기를 창고에 돌려놓고 보고하기"]}

좋은 출력 예시 2:
{"context_ko": "숲 근처에서 길 잃은 여행자가 목격됐다는 이야기가 돌고 있다.", "goal_ko": "길 잃은 여행자를 찾아 마을까지 안전하게 호위하기", "steps_ko": ["무기를 챙기고 숲 입구로 이동하기", "발자국을 따라 여행자의 위치를 찾기", "여행자를 만나 마을까지 호위하기"]}"""


def teacher_call(rec: dict, timeout=90):
    steps_txt = "; ".join(
        f"{s['type']}({', '.join(f'{k}={v}' for k, v in s.items() if k != 'type')})" for s in rec["steps"]
    )
    user = (
        f"NPC: {rec['npc']}\n원 캐릭터: {rec['character']}\n"
        f"동기: {rec['motivation']}\n목표(영문): {rec['goal_en']}\n"
        f"행동 시퀀스: {steps_txt}\n\n이를 {rec['npc']} 의 한국어 plan 으로 재작성."
    )
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "system", "content": SYSTEM}, {"role": "user", "content": user}],
        "stream": False,
        "format": SCHEMA,
        "think": False,
        "options": {"temperature": 0.4, "num_ctx": 2048, "num_predict": 400},
    }).encode()
    req = urllib.request.Request(OLLAMA, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        content = json.load(resp)["message"]["content"]
    return json.loads(content)


_BAD_GOAL = re.compile(r"Elara|James|Skadi|Moca|Guard|계획|임무 수행|시스템|[A-Za-z()]|(한다|합니다|입니다)$")


def validate_ko(ko: dict) -> bool:
    """teacher 출력 형식 검증 — goal 추상어·영어·현대어·문장어미, 스텝 미완결형 거부."""
    goal = ko.get("goal_ko", "").strip()
    steps = ko.get("steps_ko", [])
    if not goal or _BAD_GOAL.search(goal) or len(goal) > 60:
        return False
    if not (2 <= len(steps) <= 4):
        return False
    for s in steps:
        s = s.strip()
        # 영어·괄호·연결어미·존댓말 문장어미·과短 명사구 거부.
        # 명사형 종결("...주변 정찰")은 허용 — 기존 골든 예시와 동일 꼴.
        if re.search(r"[A-Za-z()]", s) or len(s) < 10:
            return False
        if re.search(r"(하며|하고|이며|입니다|합니다|한다|해요|세요)$", s):
            return False
    return True


def quality_filter(recs):
    ok = []
    for r in recs:
        if not r["npc"]:
            continue
        if len(r["steps"]) < 3 or not r["motivation"]:
            continue
        if r["mode"] == "Combat":  # 비전투 다양성이 목적 — 전투 plan 은 자체 시드가 담당
            continue
        ok.append(r)
    return ok


def to_yaml(entries):
    lines = [
        "# =============================================================================",
        "# Stage2 골든 시드 초안 — LIGHT quest_stems 한국어화 (koreanize_stage2.py 산출)",
        "# teacher: gemma4-12b. 사람 검수 후 golden_plan_seed.yaml 로 승격.",
        "# 검수 기준: goal 구체성 · steps 실행 가능성 · 현지화 자연스러움 (SPEC_finetune M3)",
        "# =============================================================================",
        "",
    ]
    for i, e in enumerate(entries, 1):
        lines.append(f"- id: plan_light_{i:03d}")
        lines.append(f"  npc: {e['npc']}")
        lines.append(f"  source_goal_en: \"{e['goal_en']}\"")
        lines.append("  context: |")
        for ln in e["context_ko"].splitlines():
            lines.append(f"    {ln}")
        lines.append("  gold:")
        lines.append(f"    goal: \"{e['goal_ko']}\"")
        lines.append("    steps:")
        for s in e["steps_ko"]:
            lines.append(f"      - \"{s}\"")
        lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=100, help="목표 시드 수")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    base = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(base, "../.."))  # finetune
    src = os.path.join(root, "data/processed/stage2_light.jsonl")
    out_yaml = os.path.join(base, "golden_plan_seed_light.yaml")
    out_fail = os.path.join(root, "data/processed/koreanize_failures.jsonl")

    recs = [json.loads(l) for l in open(src, encoding="utf-8")]
    pool = quality_filter(recs)
    print(f"전체 {len(recs)} → 품질필터 통과 {len(pool)}")

    # NPC 별 균형 샘플링
    random.seed(args.seed)
    by_npc = {}
    for r in pool:
        by_npc.setdefault(r["npc"], []).append(r)
    per = max(1, args.n // len(by_npc))
    picked = []
    for npc, lst in by_npc.items():
        random.shuffle(lst)
        picked += lst[:per]
    picked = picked[: args.n]
    print(f"샘플 {len(picked)} (NPC별 ~{per})")

    entries, fails = [], []
    for i, r in enumerate(picked, 1):
        try:
            ko = teacher_call(r)
            if not validate_ko(ko):  # 1회 재시도 (검증 실패분)
                ko = teacher_call(r)
            if not validate_ko(ko):
                raise ValueError(f"검증 실패: {ko.get('goal_ko','')[:40]}")
            entries.append({**r, **ko})
        except Exception as e:
            fails.append({"goal_en": r["goal_en"], "err": str(e)})
        if i % 10 == 0:
            print(f"  {i}/{len(picked)} (성공 {len(entries)}, 실패 {len(fails)})", flush=True)

    with open(out_yaml, "w", encoding="utf-8") as fp:
        fp.write(to_yaml(entries))
    with open(out_fail, "w", encoding="utf-8") as fp:
        for f_ in fails:
            fp.write(json.dumps(f_, ensure_ascii=False) + "\n")
    print(f"완료: 성공 {len(entries)} → {out_yaml} | 실패 {len(fails)} → {out_fail}")


if __name__ == "__main__":
    main()
