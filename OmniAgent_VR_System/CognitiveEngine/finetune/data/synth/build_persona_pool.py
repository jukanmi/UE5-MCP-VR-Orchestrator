# -*- coding: utf-8 -*-
"""범용 페르소나 풀 구축 — LIGHT(1,755명)·NPC-Dialogue_v2(1,688명) 바이오를
teacher(gemma4-12b)가 서빙 페르소나 카드 형식(name/role/traits/speech_style)으로 각색.

목적: 파인튜닝을 코어 5 NPC 에 과적합시키지 않고 "system 프롬프트의 페르소나를
따르는 능력" 자체를 학습 — 신규 NPC 를 YAML 추가만으로 대응 (재학습 불필요).

- 중세 판타지 밖 세팅(현대·SF)은 teacher 가 중세식으로 각색, 불가하면 거부
- 출력: persona_pool.yaml — generate_stage1/2 가 범용 소스로 소비
사용: python build_persona_pool.py [--n 300] [--seed 42]
"""

import argparse
import glob
import json
import os
import random
import re
import urllib.request

OLLAMA = "http://localhost:11434/api/chat"
TEACHER = "gemma4-12b"

_HERE = os.path.dirname(os.path.abspath(__file__))
_FINETUNE = os.path.abspath(os.path.join(_HERE, "../.."))  # finetune 루트
LIGHT_GLOB = os.path.join(_FINETUNE, "data/raw/light/quest_stems/*.json")
NPCV2_PARQUET = os.path.join(_FINETUNE, "data/raw/hf/npc_info.parquet")
OUT = os.path.join(_HERE, "persona_pool.yaml")

SCHEMA = {
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "캐릭터 이름 (원문 유지 또는 자연스러운 음차)"},
        "role": {"type": "string", "description": "직업/역할 짧은 영어 구 (예: Blacksmith)"},
        "traits": {
            "type": "array",
            "items": {"type": "string"},
            "minItems": 3,
            "maxItems": 4,
            "description": "성격 형용사 영어 3~4개",
        },
        "speech_style": {
            "type": "array",
            "items": {"type": "string"},
            "minItems": 3,
            "maxItems": 4,
            "description": "첫 항목=말투 설명(한국어), 이후=대사 예시(한국어) 2~3개",
        },
    },
    "required": ["name", "role", "traits", "speech_style"],
}

SYSTEM = """중세 판타지 마을 VR 게임의 NPC 페르소나 카드 작성자다.
영문 캐릭터 소개를 받아 게임 페르소나 카드로 각색하라.

규칙:
- role: 짧은 영어 직업/역할 구 (예: Blacksmith, Tavern Keeper, Wandering Bard)
- traits: 영어 형용사 3~4개 (예: Gruff, Loyal, Superstitious)
- speech_style: 첫 항목은 말투를 설명하는 한국어 한 문장, 이후 2~3개는 그 말투의 실제 대사 예시(한국어).
  대사 예시는 자연스러운 구어 — 페르소나 성격이 바로 드러나야 한다
- 현대·SF 요소는 중세 판타지로 각색 (총→석궁, 도시→마을). 각색 불가능하면 name 을 빈 문자열로
- 이름은 원문 유지 (예: Bikram). 수식어 붙은 경우 핵심 이름만

speech_style 두 번째 항목부터는 **캐릭터가 입으로 직접 말하는 대사**여야 한다.
나쁜 예(설명문 — 금지): "자신의 동료나 기술을 언급할 때 나타나는 특징적인 단어들.", "강한 의지를 가진 인물입니다."
좋은 예(대사): "쇠는 거짓말 안 해. 사람이 문제지.", "고칠 거면 두고 가. 내일 아침에 와."

현대 캐릭터(요원·과학자·형사 등)는 중세 직업으로 완전히 바꿔라 (요원→왕실 밀사, 과학자→연금술사). 원작 고유명사(MI6 등) 사용 금지.

예시 출력:
{"name": "Tomas", "role": "Village Blacksmith", "traits": ["Gruff", "Honest", "Hardworking"],
 "speech_style": ["투박하고 직설적인 반말. 일 얘기만 짧게.", "쇠는 거짓말 안 해. 사람이 문제지.", "고칠 거면 두고 가. 내일 아침에 와."]}"""


def light_bios():
    seen = set()
    for f in glob.glob(LIGHT_GLOB):
        try:
            d = json.load(open(f, encoding="utf-8"))["data"]
        except Exception:
            continue
        char = (d.get("character") or "").strip()
        persona = (d.get("persona") or "").strip()
        if not char or not persona or char in seen:
            continue
        # 동물/괴수 제외 — 사람형 페르소나만 (대화 학습 대상)
        if re.search(
            r"\b(deer|bird|butterfly|worm|rat|cat|dog|horse|wolf|bear|fish|dragon|spider|snake|goat|sheep|cow|pig|chicken|monster|beast)\b",
            char,
            re.I,
        ):
            continue
        seen.add(char)
        yield {"source": "light", "bio": f"{char}: {persona}"}


def npcv2_bios():
    import pandas as pd

    if not os.path.exists(NPCV2_PARQUET):
        return
    df = pd.read_parquet(NPCV2_PARQUET)
    for _, row in df.iterrows():
        bio = str(row.get("Character Bio") or "")[:600]
        info = str(row.get("Extracted Info") or "")[:200]
        if len(bio) < 50:
            continue
        yield {"source": "npcv2", "bio": f"{info}\n{bio}"}


def teacher_card(bio: str, timeout=60):
    body = json.dumps(
        {
            "model": TEACHER,
            "messages": [
                {"role": "system", "content": SYSTEM},
                {"role": "user", "content": f"캐릭터 소개:\n{bio}\n\n페르소나 카드로 각색."},
            ],
            "stream": False,
            "format": SCHEMA,
            "think": False,
            "options": {"temperature": 0.5, "num_ctx": 2048, "num_predict": 350},
        }
    ).encode()
    try:
        req = urllib.request.Request(OLLAMA, data=body, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(json.load(resp)["message"]["content"])
    except Exception:
        return None


def validate(card: dict) -> bool:
    if not card:
        return False
    name = (card.get("name") or "").strip()
    role = (card.get("role") or "").strip()
    traits = card.get("traits") or []
    style = card.get("speech_style") or []
    if not name or not role or len(name) > 40 or len(role) > 40:
        return False
    if not (3 <= len(traits) <= 4) or not (3 <= len(style) <= 4):
        return False
    # 코어 5 NPC 와 이름 충돌 금지
    if name in ("Elara", "James", "Skadi", "Moca", "Guard", "Player"):
        return False
    # 현대·원작 캐릭터 각색 실패 거부 (role 영어 기준)
    if re.search(r"agent|spy|detective|scientist|hacker|pilot|streamer|officer|engineer|doctor",
                 role, re.I):
        return False
    # traits: 짧은 형용사만 (구절 누출 거부)
    for t in traits:
        if len(t) > 16 or len(t.split()) > 2:
            return False
    for i, s in enumerate(style):
        if not re.search(r"[가-힣]", s):
            return False
        # 2번째 항목부터는 대사 — 설명문 어휘·현대어 거부
        if i >= 1 and re.search(r"말투|단어|키워드|인물|특징|묘사|시스템|기술|보안|첩보|요원|스트리머|카메라", s):
            return False
        if i >= 1 and re.search(r"(입니다|합니다|습니다)[.!?]?$", s.strip()):
            return False
    return True


def to_yaml(cards):
    lines = [
        "# =============================================================================",
        "# 범용 페르소나 풀 — build_persona_pool.py 산출 (teacher gemma4-12b)",
        "# 소스: LIGHT quest_stems + NPC-Dialogue_v2 바이오 → 중세 각색",
        "# 소비처: generate_stage1/2 (코어 5 NPC : 범용 = 30:70 혼합)",
        "# =============================================================================",
        "",
    ]
    for c in cards:
        lines.append(f"- name: {c['name']}")
        lines.append(f'  role: "{c["role"]}"')
        lines.append(f"  source: {c['source']}")
        lines.append(f"  traits: {json.dumps(c['traits'], ensure_ascii=False)}")
        lines.append("  speech_style:")
        for s in c["speech_style"]:
            lines.append(f'    - "{s.replace(chr(34), chr(39))}"')
        lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=300, help="목표 카드 수")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    bios = list(light_bios()) + list(npcv2_bios())
    rng.shuffle(bios)
    print(f"바이오 풀 {len(bios)} (목표 카드 {args.n})")

    cards, names, fails = [], set(), 0
    for i, b in enumerate(bios, 1):
        if len(cards) >= args.n:
            break
        card = teacher_card(b["bio"])
        if validate(card) and card["name"] not in names:
            card["source"] = b["source"]
            names.add(card["name"])
            cards.append(card)
        else:
            fails += 1
        if i % 20 == 0:
            print(f"  시도 {i} → 카드 {len(cards)} (실패 {fails})", flush=True)

    with open(OUT, "w", encoding="utf-8") as fp:
        fp.write(to_yaml(cards))
    from collections import Counter

    src_c = Counter(c["source"] for c in cards)
    print(f"완료: {len(cards)}카드 → {OUT} | 소스 분포 {dict(src_c)} | 실패 {fails}")


if __name__ == "__main__":
    main()
