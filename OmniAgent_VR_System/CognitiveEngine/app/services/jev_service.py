"""jev_service.py — 오픈소스 jevlike(vinnylarouge/jevlike) 로컬 인프로세스 전술 편향기.

역할: UE5 가 사전 정규화한 전투 지표(hp_pct/distance_m/enemy_count/is_flanked)를 받아
      단일 패스로 stance(Choice)·승수(Score)·유해 확률(Noul)을 5~20ms 내 산출한다.
      LLM 이 아니다 — 좌표·절대 HP 를 넣지 말 것(산술 능력 없음, SPEC §4.1).

폴백: jevlike 미설치 또는 체크포인트 부재·로드 실패 시 규칙 기반 휴리스틱으로 동일 형식 반환.
      어느 경로든 예외를 밖으로 내지 않는다 — UE5 는 미도착/저신뢰도 시 승수 1.0 중립으로 자체 폴백.

jevlike 실제 API(README 기준): jevlike.model.load_checkpoint / jevlike.data.ChoiceExample /
jevlike.train.move — SPEC §7.1 의 `jevlike.scorer.OptionScorer` 는 존재하지 않아 실제 모듈에 맞췄다.
"""

from __future__ import annotations

import logging
import math
import os
import time
from typing import Any, Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)

# 옵션 순서 고정 — 학습 JSONL label 인덱스(0=aggressive,1=defensive,2=flee)와 1:1.
OPTIONS: Tuple[str, str, str] = ("aggressive", "defensive", "flee")
# UE5 EJevTacticalStance 문자열(C++ ParseJevStance 와 철자 일치 필수).
STANCES: Tuple[str, str, str] = ("Aggressive", "Defensive", "Flee")

DEFAULT_CHECKPOINT = os.environ.get("JEV_CHECKPOINT", "app/models/jevlike_tactics.pt")


def build_context(metrics: Dict[str, Any]) -> str:
    """지표 → 20토큰 미만 초경량 컨텍스트(SPEC §4.2). 학습 JSONL 의 context 와 같은 포맷."""
    try:
        hp = float(metrics.get("hp_pct", 1.0))
        dist = float(metrics.get("distance_m", 5.0))
        count = int(metrics.get("enemy_count", 1))
        flanked = bool(metrics.get("is_flanked", False))
    except (TypeError, ValueError):
        hp, dist, count, flanked = 1.0, 5.0, 1, False
    return f"hp:{hp:.2f} dist:{dist:.1f} count:{count} flanked:{str(flanked).lower()}"


def _softmax(logits: List[float]) -> List[float]:
    m = max(logits)
    exps = [math.exp(x - m) for x in logits]
    total = sum(exps)
    return [e / total for e in exps]


def heuristic_probs(metrics: Dict[str, Any]) -> List[float]:
    """규칙 기반 폴백 — 체크포인트 없이도 단조적(HP↓·적↑·포위 → flee↑)인 확률 분포.

    ponytail: 손튜닝 로짓. 학습 체크포인트가 생기면 이 함수는 폴백으로만 남는다.
    """
    try:
        hp = min(1.0, max(0.0, float(metrics.get("hp_pct", 1.0))))
        dist = max(0.0, float(metrics.get("distance_m", 5.0)))
        extra = max(0, int(metrics.get("enemy_count", 1)) - 1)
        flanked = 1.0 if metrics.get("is_flanked", False) else 0.0
    except (TypeError, ValueError):
        return [1 / 3, 1 / 3, 1 / 3]

    loss = 1.0 - hp
    aggressive = 2.0 * hp - 0.6 * extra - 0.8 * flanked - 0.1 * max(0.0, dist - 3.0)
    defensive = 0.4 + 0.5 * extra + 0.6 * flanked + 1.0 * loss
    flee = 4.0 * loss * loss + 0.4 * extra + 0.6 * flanked - 0.8
    return _softmax([aggressive, defensive, flee])


class JevlikeService:
    """체크포인트가 있으면 jevlike 모델, 없으면 휴리스틱. 두 경로 모두 evaluate_tactics 형식 동일."""

    def __init__(self, checkpoint_path: str = DEFAULT_CHECKPOINT, device: str = "auto") -> None:
        self.checkpoint_path = checkpoint_path
        self.options = list(OPTIONS)
        self._model: Any = None
        self._collator: Any = None
        self._device: Any = None
        self._load(device)

    # ── 모델 로드 ────────────────────────────────────────────────────────
    def _load(self, device: str) -> None:
        if not os.path.exists(self.checkpoint_path):
            logger.info(f"[Jev] 체크포인트 없음({self.checkpoint_path}) → 휴리스틱 폴백")
            return
        try:
            from jevlike.model import load_checkpoint, select_device  # type: ignore

            self._device = select_device(device)
            self._model, self._collator, _ = load_checkpoint(self.checkpoint_path, self._device)
            self._model.eval()
            logger.info(f"[Jev] jevlike 로드 완료 device={self._device}")
        except Exception as e:  # ImportError 포함 — 어떤 실패든 휴리스틱으로
            self._model = None
            logger.warning(f"[Jev] jevlike 로드 실패({e}) → 휴리스틱 폴백")

    @property
    def model_loaded(self) -> bool:
        return self._model is not None

    # ── 추론 ────────────────────────────────────────────────────────────
    def _model_probs(self, context: str) -> Optional[List[float]]:
        """jevlike 단일 패스. 실패 시 None(휴리스틱 폴백)."""
        if self._model is None:
            return None
        try:
            import torch  # type: ignore
            from jevlike.data import ChoiceExample  # type: ignore
            from jevlike.train import move  # type: ignore

            batch = move(self._collator([ChoiceExample(context, tuple(self.options), 0)]), self._device)
            with torch.no_grad():
                return self._model(batch).softmax(-1)[0, : len(self.options)].cpu().tolist()
        except Exception as e:
            logger.warning(f"[Jev] 추론 실패({e}) → 휴리스틱 폴백")
            return None

    def evaluate_tactics(self, metrics: Dict[str, Any]) -> Dict[str, Any]:
        """정규화 지표 → {stance, confidence, score_aggression, score_caution, noul_harmful}.

        승수 규약: 50/50 분포에서 두 승수 모두 1.0(중립). aggression=2·P(aggr),
        caution=2·(P(def)+P(flee)). UE5 가 [0.25, 4.0] 으로 Clamp 한다.
        """
        t0 = time.perf_counter()
        metrics = metrics if isinstance(metrics, dict) else {}
        context = build_context(metrics)
        probs = self._model_probs(context) or heuristic_probs(metrics)

        best = max(range(len(probs)), key=probs.__getitem__)
        result = {
            "stance": STANCES[best],
            "confidence": float(probs[best]),
            "score_aggression": float(probs[0]) * 2.0,
            "score_caution": float(probs[1] + probs[2]) * 2.0,
            # ponytail: 유해 판별 모델 미학습 — 0.0 고정. Noul 학습 데이터가 생기면 두 번째 헤드로 교체.
            "noul_harmful": 0.0,
        }
        logger.debug(f"[Jev] {context} → {result['stance']} p={probs} {(time.perf_counter() - t0) * 1000:.2f}ms")
        return result


_SERVICE: Optional[JevlikeService] = None


def get_jev_service() -> JevlikeService:
    """지연 로드 싱글턴 — import 시점에 torch/체크포인트를 건드리지 않는다(테스트·기동 속도)."""
    global _SERVICE
    if _SERVICE is None:
        _SERVICE = JevlikeService()
    return _SERVICE
