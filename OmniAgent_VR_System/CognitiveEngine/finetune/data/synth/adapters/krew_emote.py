# -*- coding: utf-8 -*-
"""KREW 한국어 롤플레잉 → 지문(*...*) 추출·Emote 매핑 + 자연 발화 코퍼스.

산출 2종 (processed/, gitignore):
1. krew_emote.jsonl — (지문, Emote 스타일, 동반 대사) 페어. Stage1 Emote 학습·facial 다양화용
2. krew_utterances.jsonl — 짧은 인게임형 한국어 발화(지문 제거·정제). teacher paraphrase 대체 후보

지문→Emote 매핑은 키워드 규칙 — 미매칭 지문은 raw 로 남겨 후속 검토.
"""
import json, os, re, collections
import pandas as pd

# 지문 키워드 → (Emote 스타일, FacialState) — C++ emote 어휘 확정 전 임시 스타일명
EMOTE_RULES = [
    (r"웃으며|웃음|미소|킥킥|씨익|활짝", ("Smile", "Happy")),
    (r"손을 흔들|손짓", ("Wave", "Happy")),
    (r"고개를 끄덕|끄덕이", ("Nod", "Neutral")),
    (r"고개를 저|고개를 흔들", ("Shake", "Neutral")),
    (r"한숨|땅이 꺼져라", ("Sigh", "Sad")),
    (r"눈물|훌쩍|흐느끼|울먹", ("Cry", "Sad")),
    (r"화를 내|찌푸리|인상을 쓰|노려보", ("Frown", "Angry")),
    (r"놀라|눈이 커지|화들짝|헉", ("Surprise", "Surprised")),
    (r"박수|손뼉", ("Clap", "Happy")),
    (r"어깨를 으쓱", ("Shrug", "Neutral")),
    (r"고개를 숙|인사하|꾸벅", ("Bow", "Neutral")),
    (r"하품|졸린|눈을 비비", ("Yawn", "Tired")),
    (r"팔짱", ("CrossArms", "Neutral")),
    (r"손을 뻗|어루만지|쓰다듬|토닥", ("Pat", "Happy")),
    (r"반짝이|설레|들뜬", ("Excited", "Happy")),
    (r"바라보며|바라본다|응시|시선을", ("Gaze", "Neutral")),
]
ASIDE = re.compile(r"\*([^*]{2,60})\*")


def map_emote(aside: str):
    for pat, (style, facial) in EMOTE_RULES:
        if re.search(pat, aside):
            return style, facial
    return "", ""


def clean_speech(text: str) -> str:
    """지문·따옴표 제거 후 순수 대사."""
    t = ASIDE.sub("", text)
    t = t.replace('"', "").replace("“", "").replace("”", "").strip()
    return re.sub(r"\s+", " ", t)


def iter_messages(parquet_path):
    df = pd.read_parquet(parquet_path)
    col = "text"
    for _, row in df.iterrows():
        msgs = row[col]
        for m in msgs:
            yield m["role"], m["content"]


def main():
    base = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(base, "../../.."))  # finetune
    srcs = [os.path.join(root, "data/raw/hf/krew_exa.parquet"),
            os.path.join(root, "data/raw/hf/krew_gf.parquet")]
    out_dir = os.path.join(root, "data/processed")
    os.makedirs(out_dir, exist_ok=True)

    emotes, utts = [], []
    style_c, unmapped = collections.Counter(), collections.Counter()
    seen_utt = set()
    for src in srcs:
        if not os.path.exists(src):
            print(f"skip(없음): {src}")
            continue
        for role, content in iter_messages(src):
            if role != "assistant" or not isinstance(content, str):
                continue
            asides = ASIDE.findall(content)
            speech = clean_speech(content)
            for a in asides:
                style, facial = map_emote(a)
                if style:
                    style_c[style] += 1
                    emotes.append({"aside": a.strip(), "style": style, "facial": facial,
                                   "speech": speech[:120]})
                else:
                    unmapped[a.strip()[:30]] += 1
            # 발화 코퍼스: 짧은 인게임형(6~60자)·중복 제거
            if 6 <= len(speech) <= 60 and speech not in seen_utt:
                seen_utt.add(speech)
                utts.append({"utterance": speech})

    with open(os.path.join(out_dir, "krew_emote.jsonl"), "w", encoding="utf-8") as fp:
        for e in emotes:
            fp.write(json.dumps(e, ensure_ascii=False) + "\n")
    with open(os.path.join(out_dir, "krew_utterances.jsonl"), "w", encoding="utf-8") as fp:
        for u in utts:
            fp.write(json.dumps(u, ensure_ascii=False) + "\n")

    print(f"Emote 페어 {len(emotes)} | 발화 {len(utts)}")
    print("=== Emote 스타일 분포 ===")
    for s, c in style_c.most_common():
        print(f"  {s:10} {c}")
    print("=== 미매핑 지문 top10 (규칙 확장 후보) ===")
    for a, c in unmapped.most_common(10):
        print(f"  {c:4} {a}")


if __name__ == "__main__":
    main()
