"""fit_combat_heuristic.py — 전투 stance 휴리스틱 로짓 계수를 LLM 라벨(combos_combat.jsonl)에 피팅.

전투 입력은 4개 지표뿐이라 모델 학습 대신 heuristic_probs 의 선형 로짓 계수만 라벨에 맞춘다.
- 피팅 대상: 상수·hp·hp²·추가 적 수·포위 (클래스별 5계수, L2 정규화 — 라벨의 aggressive 0% 구간에서 로짓 발산 방지)
- 고정: 거리 항 −0.1·max(0, dist−3) (라벨러가 거리를 무시해 라벨로는 추정 불가 — 설계값 유지)
- 성격 돌파 항은 라벨에 성격이 없어 여기서 다루지 않는다(jev_service.apply_personality)

실행(CognitiveEngine 에서): ../../.venv/Scripts/python.exe finetune/jev/fit_combat_heuristic.py
출력: 계수 표(heuristic_probs 에 옮겨 적을 값) + 80/20 홀드아웃에서 현행 휴리스틱 대비 정확도·log-loss.
"""

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from app.services.jev_service import OPTIONS, heuristic_probs  # noqa: E402

DATA = Path(__file__).resolve().parent / "data" / "combos_combat.jsonl"
FEATURES = ["bias", "hp", "hp2", "extra", "flanked"]
L2 = 0.003  # 0 이면 계수 ~15 로 라벨의 aggressive 0% 를 그대로 재현, 0.05 면 과평탄(log-loss 악화) — 스윕 결과


def features(m: dict) -> np.ndarray:
    hp = min(1.0, max(0.0, float(m["hp_pct"])))
    return np.array([1.0, hp, hp * hp, max(0, int(m["enemy_count"]) - 1), 1.0 if m["is_flanked"] else 0.0])


def dist_term(m: dict) -> np.ndarray:
    return np.array([-0.1 * max(0.0, float(m["distance_m"]) - 3.0), 0.0, 0.0])


def softmax(z: np.ndarray) -> np.ndarray:
    z = z - z.max(axis=-1, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=-1, keepdims=True)


def fit(X: np.ndarray, D: np.ndarray, y: np.ndarray, steps: int = 30000, lr: float = 0.3) -> np.ndarray:
    W = np.zeros((3, X.shape[1]))
    Y = np.eye(3)[y]
    for _ in range(steps):
        P = softmax(X @ W.T + D)
        W -= lr * ((P - Y).T @ X / len(X) + L2 * W)
    return W - W.mean(axis=0)  # softmax 는 클래스 공통 이동에 불변 → 평균 0 으로 정규화해 읽기 쉽게


def evaluate(P: np.ndarray, y: np.ndarray) -> tuple[float, float]:
    return float((P.argmax(1) == y).mean()), float(-np.log(P[np.arange(len(y)), y] + 1e-9).mean())


def main() -> None:
    rows = [json.loads(line) for line in DATA.open(encoding="utf-8")]
    rng = np.random.default_rng(42)
    idx = rng.permutation(len(rows))
    cut = int(len(rows) * 0.8)
    X = np.stack([features(r["metrics"]) for r in rows])
    D = np.stack([dist_term(r["metrics"]) for r in rows])
    y = np.array([OPTIONS.index(r["label"]) for r in rows])
    tr, te = idx[:cut], idx[cut:]

    W = fit(X[tr], D[tr], y[tr])
    fitted = softmax(X[te] @ W.T + D[te])
    current = np.array([heuristic_probs(rows[i]["metrics"]) for i in te])

    print("계수 (행 = aggressive/defensive/flee, 열 = " + "/".join(FEATURES) + ")")
    for name, w in zip(OPTIONS, W):
        print(f"  {name:10} " + "  ".join(f"{v:+.2f}" for v in w))
    for label, P in [("현행 휴리스틱", current), ("피팅", fitted)]:
        acc, nll = evaluate(P, y[te])
        print(f"{label:8} 홀드아웃 정확도 {acc:.1%}  log-loss {nll:.3f}  (n={len(te)})")


if __name__ == "__main__":
    main()
