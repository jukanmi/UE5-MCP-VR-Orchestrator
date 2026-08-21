"""situation.sentiment 일괄 재생성 — 카테고리 무관 1:1:1 균등 랜덤 (2026-07-27, 수정판).

이전판(카테고리→sentiment 결정론 매핑, Combat=Hostile/Social=Friendly)은 스퓨리어스
상관관계를 만듦 — "적대적이면 항상 Attack" 만 학습하고 "호감도 낮은데 의자에 앉으라고
시키는" 조합을 아예 못 보게 됨. sentiment 는 situation/action 과 독립이어야 모델이
"호감도와 무관하게 명령받은 행동은 수행, 어투만 달라짐"을 배움.

사용: python finetune/dataset_editor/bulk_set_sentiment.py
"""
import os
import random

import yaml

SEED_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "synth", "scenarios_seed_draft.yaml")

# 카테고리 무관 1:1:1 균등 — situation 과 sentiment 의 독립성이 핵심.
SENTIMENT_BUCKETS = [("Hostile", -60), ("Neutral", 0), ("Friendly", 60)]

rng = random.Random(2027)


def main() -> None:
    seeds = yaml.safe_load(open(SEED_PATH, encoding="utf-8")) or []
    for s in seeds:
        sit = s.setdefault("situation", {})
        tag, score = rng.choice(SENTIMENT_BUCKETS)
        sit["sentiment"] = f"{tag} (Score: {score})"

    with open(SEED_PATH, "w", encoding="utf-8") as fp:
        yaml.safe_dump(seeds, fp, allow_unicode=True, sort_keys=False)

    from collections import Counter
    c = Counter((s["situation"]["sentiment"]) for s in seeds)
    print(f"{len(seeds)}개 시드 sentiment 1:1:1 재생성 완료")
    for k, v in sorted(c.items()):
        print(f"  {k}: {v}")


if __name__ == "__main__":
    main()
