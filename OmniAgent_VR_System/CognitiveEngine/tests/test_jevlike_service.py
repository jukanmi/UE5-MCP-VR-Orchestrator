"""jevlike 전술 편향 서비스 회귀 테스트 — 체크포인트 없는 휴리스틱 폴백 경로 + jev_query 핸들러.

검증 항목:
  - 만HP·단독 → Aggressive, 저HP·다수·포위 → Flee (단조성)
  - 반환 키·범위: confidence∈[0,1], 승수 합 2.0(50/50 = 1.0/1.0 중립), noul∈[0,1]
  - 불량 지표(문자열·None)에도 예외 없이 응답
  - 지연: 100회 평균 < 20ms (SPEC §1.3 5~20ms 예산)
  - main._handle_jev_query 가 jev_decision 봉투를 generation echo 와 함께 돌려주는지
"""

import json
import time

import pytest

from app.schemas.envelope import EEnvelopeType, JevQueryPayload, MessageEnvelope
from app.services.jev_service import STANCES, JevlikeService, build_context, heuristic_probs

REQUIRED_KEYS = {"stance", "confidence", "score_aggression", "score_caution", "noul_harmful"}


@pytest.fixture(scope="module")
def service() -> JevlikeService:
    # 존재하지 않는 경로 → 휴리스틱 폴백 강제(CI 에 체크포인트 없음).
    svc = JevlikeService(checkpoint_path="nonexistent/jevlike_tactics.pt")
    assert not svc.model_loaded
    return svc


def test_full_hp_alone_is_aggressive(service: JevlikeService) -> None:
    out = service.evaluate_tactics({"hp_pct": 1.0, "distance_m": 2.0, "enemy_count": 1, "is_flanked": False})
    assert out["stance"] == "Aggressive"
    assert out["score_aggression"] > 1.0 > out["score_caution"]


def test_low_hp_flanked_is_flee(service: JevlikeService) -> None:
    out = service.evaluate_tactics({"hp_pct": 0.15, "distance_m": 2.5, "enemy_count": 3, "is_flanked": True})
    assert out["stance"] == "Flee"
    assert out["score_caution"] > 1.0 > out["score_aggression"]


def test_result_shape_and_ranges(service: JevlikeService) -> None:
    out = service.evaluate_tactics({"hp_pct": 0.5, "distance_m": 5.0, "enemy_count": 1, "is_flanked": False})
    assert set(out) == REQUIRED_KEYS
    assert out["stance"] in STANCES
    assert 0.0 <= out["confidence"] <= 1.0
    assert 0.0 <= out["noul_harmful"] <= 1.0
    # 승수 규약: aggression + caution == 2.0 → 50/50 이면 둘 다 1.0 중립.
    assert out["score_aggression"] + out["score_caution"] == pytest.approx(2.0)


def test_garbage_metrics_do_not_raise(service: JevlikeService) -> None:
    for bad in ({}, {"hp_pct": "abc"}, {"hp_pct": None, "enemy_count": "x"}, None):
        out = service.evaluate_tactics(bad)  # type: ignore[arg-type]
        assert set(out) == REQUIRED_KEYS
    assert heuristic_probs({"hp_pct": "abc"}) == pytest.approx([1 / 3] * 3)


def test_context_is_token_diet() -> None:
    ctx = build_context({"hp_pct": 0.35, "distance_m": 4.2, "enemy_count": 2, "is_flanked": True})
    assert ctx == "hp:0.35 dist:4.2 count:2 flanked:true"
    assert len(ctx.split()) <= 5


def test_latency_budget(service: JevlikeService) -> None:
    metrics = {"hp_pct": 0.35, "distance_m": 4.2, "enemy_count": 2, "is_flanked": True}
    t0 = time.perf_counter()
    for _ in range(100):
        service.evaluate_tactics(metrics)
    avg_ms = (time.perf_counter() - t0) * 1000 / 100
    assert avg_ms < 20.0, f"평균 {avg_ms:.2f}ms — 5~20ms 예산 초과"


def test_handle_jev_query_returns_decision_envelope(monkeypatch: pytest.MonkeyPatch, service: JevlikeService) -> None:
    from app import main

    monkeypatch.setattr(main, "get_jev_service", lambda: service)
    env = MessageEnvelope(
        msg_id="t1",
        auth_token="x",
        type=EEnvelopeType.JEV_QUERY,
        payload={
            "npc_id": "Guard_01",
            "generation": 42,
            "metrics": {"hp_pct": 0.35, "distance_m": 4.2, "enemy_count": 2, "is_flanked": True},
        },
    )
    out = json.loads(main._handle_jev_query(env))
    assert out["type"] == EEnvelopeType.JEV_DECISION.value == "jev_decision"
    p = out["payload"]
    assert p["npc_id"] == "Guard_01" and p["generation"] == 42
    assert REQUIRED_KEYS <= set(p)
    assert JevQueryPayload(**env.payload).generation == 42


def test_handle_jev_query_neutral_on_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    """서비스 예외 → 무음 드랍 대신 confidence 0.0 중립 응답(UE5 는 승수 1.0 유지)."""
    from app import main

    class Boom:
        def evaluate_tactics(self, _m):
            raise RuntimeError("boom")

    monkeypatch.setattr(main, "get_jev_service", lambda: Boom())
    env = MessageEnvelope(msg_id="t2", auth_token="x", type=EEnvelopeType.JEV_QUERY, payload={"npc_id": "G"})
    p = json.loads(main._handle_jev_query(env))["payload"]
    assert p["confidence"] == 0.0 and p["score_aggression"] == 1.0 and p["score_caution"] == 1.0
