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
import random
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
    def _model_probs(self, context: str, options: Optional[List[str]] = None) -> Optional[List[float]]:
        """jevlike 단일 패스. 실패 시 None(휴리스틱 폴백). options 미지정 = 전투 3택."""
        if self._model is None:
            return None
        options = options or self.options
        try:
            import torch  # type: ignore
            from jevlike.data import ChoiceExample  # type: ignore
            from jevlike.train import move  # type: ignore

            batch = move(self._collator([ChoiceExample(context, tuple(options), 0)]), self._device)
            with torch.no_grad():
                return self._model(batch).softmax(-1)[0, : len(options)].cpu().tolist()
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

    # ── daily(비전투 일상 활동) — SPEC_jev_daily D4 ────────────────────────────
    def evaluate_daily(
        self,
        metrics: Dict[str, Any],
        activities: List[str],
        pools: Dict[str, Any],
        persona: Optional[Dict[str, Any]] = None,
        rng: Optional[random.Random] = None,
    ) -> Dict[str, Any]:
        """활동 1패스 + 슬롯 순차 패스(최대 4). 어떤 예외든 stay(= 현행 Idle)로 돌려준다.

        반환: {activity, slots{슬롯: 후보 id | "default"}, confidence(활동 패스 확률), passes}.
        """
        try:
            return self._evaluate_daily(metrics or {}, activities or [], pools or {}, persona or {}, rng or random)
        except Exception as e:
            logger.warning(f"[Jev] daily 평가 실패({e}) → stay")
            return {"activity": "stay", "slots": {}, "confidence": 0.0, "passes": 0}

    def _evaluate_daily(
        self, metrics: Dict[str, Any], activities: List[str], pools: Dict[str, Any], persona: Dict[str, Any], rng: Any
    ) -> Dict[str, Any]:
        t0 = time.perf_counter()
        ctx = _DailyCtx(metrics, persona)
        # 목록 밖·command 전용 활동은 버리고 stay 는 항상 넣는다(탈출구).
        acts = [a for a in dict.fromkeys(activities) if a in ACTIVITY_SLOTS and a not in DAILY_BLOCKED]
        if "stay" not in acts:
            acts.append("stay")

        passes = 0
        if len(acts) == 1:
            activity, confidence = acts[0], 1.0
        else:
            probs = self._pass_probs(ctx.text("activity"), acts, [activity_logit(a, ctx) for a in acts])
            idx = _sample(probs, rng)
            activity, confidence = acts[idx], float(probs[idx])
            passes += 1

        slots: Dict[str, str] = {}
        ctx.chosen["activity"] = activity
        for slot in ACTIVITY_SLOTS[activity]:
            cands = slot_candidates(activity, slot, pools)
            if len(cands) <= 1:  # 후보 0개 = default, 1개 = 그대로 — 추론 생략
                value = cands[0][0] if cands else DEFAULT
            else:
                ids = [c[0] for c in cands] + [DEFAULT]
                logits = [slot_logit(activity, slot, cid, desc, ctx) for cid, desc in cands] + [DEFAULT_LOGIT]
                value = ids[_sample(self._pass_probs(ctx.text(slot), ids, logits), rng)]
                passes += 1
            slots[slot] = value
            ctx.chosen[slot] = value

        logger.debug(f"[Jev] daily {activity} {slots} passes={passes} {(time.perf_counter() - t0) * 1000:.2f}ms")
        return {"activity": activity, "slots": slots, "confidence": confidence, "passes": passes}

    def _pass_probs(self, context: str, options: List[str], logits: List[float]) -> List[float]:
        """모델이 있으면 모델, 없으면 휴리스틱 로짓 softmax. 온도는 샘플링(_sample)이 적용."""
        return self._model_probs(context, options) or _softmax(logits)


_SERVICE: Optional[JevlikeService] = None


def get_jev_service() -> JevlikeService:
    """지연 로드 싱글턴 — import 시점에 torch/체크포인트를 건드리지 않는다(테스트·기동 속도)."""
    global _SERVICE
    if _SERVICE is None:
        _SERVICE = JevlikeService()
    return _SERVICE


# ─────────────────────────────────────────────────────────────────────────────
# daily 휴리스틱 (M1). M2 에서 같은 체크포인트가 학습되면 폴백으로만 남는다.
# ─────────────────────────────────────────────────────────────────────────────

DEFAULT = "default"
# default 는 "C++ 기존 결정 규칙에 위임" — 맞는 후보가 없을 때의 탈출구라 낮게.
DEFAULT_LOGIT = -0.5
DAILY_TEMPERATURE = float(os.environ.get("JEV_DAILY_TEMPERATURE", "1.0"))

MOVE_STYLES = ("Walk", "Run", "Crouch")
FACIALS = ("Neutral", "Happy", "Sad", "Tired", "Surprised")
# command 트리거 전용 — daily 에서는 C++ 가 보내도 고르지 않는다.
DAILY_BLOCKED = frozenset({"follow", "equip"})

# 활동 → 슬롯 순서(SPEC_jev_daily D4 활동 표).
ACTIVITY_SLOTS: Dict[str, Tuple[str, ...]] = {
    "stay": (),
    "stand_up": (),
    "look_at": ("target", "facial"),
    "wander": ("dest", "style"),
    "patrol": ("dest",),
    "follow": ("target", "style"),
    "rest": ("target",),
    "emote": ("style", "target", "facial"),
    "use_item": ("item",),
    "equip": ("item",),
    "give_item": ("target", "item"),
    "pick_up": ("target",),
}

_WATCHFUL = {"disciplined", "cautious", "observant"}
_DEVOUT = {"gentle", "guilt-ridden"}
_GUARD_ROLES = ("guard", "keeper", "knight", "commander", "soldier")


def _pool(pools: Dict[str, Any], key: str) -> List[Tuple[str, str]]:
    """풀 → (id, desc) 목록. media 처럼 문자열 목록이면 desc = id."""
    out: List[Tuple[str, str]] = []
    for e in pools.get(key) or []:
        if isinstance(e, dict) and e.get("id"):
            out.append((str(e["id"]), str(e.get("desc", ""))))
        elif isinstance(e, str) and e:
            out.append((e, e))
    return out


def _desc_head(desc: str) -> str:
    return desc.split("|", 1)[0].strip().lower()


def slot_candidates(activity: str, slot: str, pools: Dict[str, Any]) -> List[Tuple[str, str]]:
    """(활동, 슬롯) 의 실제 후보. default 는 여기 넣지 않는다(패스에서 붙임)."""
    actors, places, pois = _pool(pools, "actors"), _pool(pools, "places"), _pool(pools, "pois")
    if slot == "facial":
        return [(f, f) for f in FACIALS]
    if slot == "style":
        if activity == "emote":
            return _pool(pools, "media")
        return [(m, m) for m in MOVE_STYLES]
    if slot == "dest":
        if activity == "patrol":
            return pois
        return pois + places + actors + [("random", "random")]
    if slot == "item":
        items = _pool(pools, "items")
        if activity == "use_item":
            return [i for i in items if _desc_head(i[1]) == "consumable"]
        if activity == "equip":
            return [i for i in items if _desc_head(i[1]) == "equipment"] + _pool(pools, "equipped")
        return [i for i in items if _desc_head(i[1]) != "quest"]  # give_item
    # target
    if activity == "look_at":
        return actors + places + pois + [("around", "around")]
    if activity == "emote":
        return actors + [("none", "none")]
    if activity == "rest":
        return [p for p in places if "vacant" in p[1].lower()]
    if activity == "give_item":
        # daily: NPC 대상만 — Player(경제·퀘스트)·ground(버리기)는 command 전용.
        return [a for a in actors if _desc_head(a[1]) == "npc"]
    if activity == "pick_up":
        return _pool(pools, "ground_items")
    return actors  # follow


def _num(v: Any, default: float) -> float:
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def _dist_m(desc: str) -> float:
    for tok in desc.split("|"):
        tok = tok.strip().lower()
        if tok.endswith("m"):
            try:
                return float(tok[:-1])
            except ValueError:
                pass
    return 10.0


class _DailyCtx:
    """패스들이 공유하는 판단 재료. chosen 은 앞 패스 선택 누적(context 에 붙는다)."""

    def __init__(self, metrics: Dict[str, Any], persona: Dict[str, Any]) -> None:
        self.metrics = metrics
        traits = persona.get("traits") or []
        self.traits = {str(t).strip().lower() for t in (traits if isinstance(traits, list) else [traits])}
        self.role = str(persona.get("role") or "")
        self.chosen: Dict[str, str] = {}
        self.player_dist = _num(metrics.get("player_dist_m"), -1.0)
        self.posture = str(metrics.get("posture") or "stand")
        self.posture_s = _num(metrics.get("posture_s"), 0.0)

    @property
    def player_near(self) -> bool:
        return 0.0 <= self.player_dist <= 5.0

    def text(self, slot: str) -> str:
        """모델 입력 context — 학습 JSONL(M2)과 같은 포맷. 산술 금지라 수치는 반올림만."""
        m = self.metrics
        chosen = ", ".join(f"{k}={v}" for k, v in self.chosen.items())
        return (
            f"[daily] pick:{slot} posture:{self.posture} player:{self.player_dist:.0f}m/{m.get('player_relation', '')} "
            f"npc_near:{m.get('npc_near', 0)} last:{m.get('last_activity', '')} role:{self.role} "
            f"traits:{','.join(sorted(self.traits))}" + (f" chosen: {chosen}" if chosen else "")
        )


def activity_logit(activity: str, ctx: _DailyCtx) -> float:
    """ponytail: 손튜닝 로짓(D7). 학습 체크포인트가 생기면 폴백으로만 남는다."""
    v = {
        "stay": 0.3,
        "stand_up": -0.5,
        "look_at": 0.6,
        "wander": 0.8,
        "patrol": 0.2,
        "rest": 0.5,
        "emote": 0.6,
        "use_item": -0.5,
        "give_item": -0.8,
        "pick_up": 0.2,
    }.get(activity, 0.0)
    if ctx.player_near:
        v += {"rest": -1.0, "look_at": 0.8, "stay": 0.5, "emote": 0.3}.get(activity, 0.0)
    if activity == "stand_up" and ctx.posture in ("sit", "lie"):
        # 앉자마자·눕자마자 일어나지 않게 — 2분 전엔 억제, 넘으면 크게 올린다.
        v += 2.5 if ctx.posture_s > 120 else -1.5
    if ctx.traits & _WATCHFUL and activity in ("look_at", "wander", "patrol"):
        v += 0.7
    if ctx.traits & _DEVOUT and activity == "emote":
        v += 0.5
    if activity == "patrol":
        v += 1.0 if any(r in ctx.role.lower() for r in _GUARD_ROLES) else -1.0
    if activity == ctx.metrics.get("last_activity"):
        v -= 1.5  # 반복 억제
    return v


def slot_logit(activity: str, slot: str, cid: str, desc: str, ctx: _DailyCtx) -> float:
    if slot == "facial":
        v = {"Neutral": 1.0, "Happy": 0.3, "Sad": -0.5, "Tired": -1.0, "Surprised": -1.5}.get(cid, 0.0)
        if cid == "Happy" and "gentle" in ctx.traits:
            v += 1.0
        if cid == "Sad" and "guilt-ridden" in ctx.traits:
            v += 1.2
        if cid == "Tired":
            v += min(ctx.posture_s, 600.0) / 200.0
        return v
    if slot == "style" and activity == "emote":
        family = cid.split("_", 1)[0].lower()
        v = 0.5 if family == "emote" else 0.0
        if family == "dance" and (ctx.player_near or ctx.traits & _WATCHFUL):
            v -= 1.0
        if family == "emote" and ctx.player_near:
            v += 0.5
        if family == "pray" and ctx.traits & _DEVOUT:
            v += 1.5
        return v
    if slot == "style":
        return {"Walk": 1.5, "Run": -0.5, "Crouch": -1.0 + (1.0 if "cautious" in ctx.traits else 0.0)}.get(cid, 0.0)
    if slot == "item":
        count = 1.0
        for tok in desc.split("|"):
            if tok.startswith("x"):
                count = _num(tok[1:], 1.0)
        return 0.2 * min(count, 5.0)
    # target / dest
    if cid in ("around", "none", "random"):
        return {"around": 0.2, "none": 0.3, "random": 0.0}[cid]
    d = desc.lower()
    v = -0.1 * _dist_m(d)
    if "friendly" in d:
        v += 1.0
    if "hostile" in d:
        v -= 5.0  # daily 후보에서 적대는 사실상 배제
    if "near_player" in d and ctx.traits & {"gentle", "attentive", "soft-spoken"}:
        v += 0.5
    return v


def _sample(probs: List[float], rng: Any) -> int:
    """분포 샘플링(argmax 아님 — 같은 상황에서도 다양성). 온도 = JEV_DAILY_TEMPERATURE."""
    t = max(DAILY_TEMPERATURE, 1e-3)
    weights = [max(p, 0.0) ** (1.0 / t) for p in probs]
    if sum(weights) <= 0:
        return 0
    return rng.choices(range(len(probs)), weights=weights, k=1)[0]
