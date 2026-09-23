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


# ─────────────────────────────────────────────────────────────────────────────
# daily(비전투 일상 활동) — SPEC_jev_daily 완료 기준 1·2
# ─────────────────────────────────────────────────────────────────────────────

ALL_DAILY = ["stay", "stand_up", "look_at", "wander", "patrol", "rest", "emote", "use_item", "give_item", "pick_up"]
POOLS = {
    "actors": [
        {"id": "Player", "desc": "player|friendly|4m"},
        {"id": "Elara", "desc": "npc|friendly|3m"},
        {"id": "Vorg", "desc": "npc|hostile|9m"},
    ],
    "places": [{"id": "Bench_03", "desc": "seat|vacant|5m|near_player"}, {"id": "Bed_01", "desc": "bed|occupied|14m"}],
    "pois": [{"id": "POI_Gate", "desc": "poi|Gate|12m"}, {"id": "POI_Well", "desc": "poi|Well|6m"}],
    "items": [
        {"id": "Apple", "desc": "consumable|x3"},
        {"id": "Rock", "desc": "general|x1"},
        {"id": "Seal", "desc": "quest|x1"},
    ],
    "ground_items": [{"id": "Item_7F3A", "desc": "Herb|2m"}],
    "media": ["Emote", "Pray", "Dance", "Sing"],
}
METRICS = {"posture": "stand", "posture_s": 0, "idle_s": 11.2, "player_dist_m": 4.1, "player_relation": "friendly"}
PERSONA = {"role": "Gate Keeper", "traits": ["Cautious", "Loyal", "Observant"]}


def _daily(service: JevlikeService, activities=ALL_DAILY, pools=POOLS, metrics=METRICS, seed: int = 0):
    import random

    return service.evaluate_daily(metrics, list(activities), pools, PERSONA, random.Random(seed))


def test_daily_returns_known_activity_and_valid_slots(service: JevlikeService) -> None:
    from app.services.jev_service import ACTIVITY_SLOTS, slot_candidates

    for seed in range(200):
        out = _daily(service, seed=seed)
        assert out["activity"] in ALL_DAILY
        assert tuple(out["slots"]) == ACTIVITY_SLOTS[out["activity"]]
        for slot, value in out["slots"].items():
            valid = {c[0] for c in slot_candidates(out["activity"], slot, POOLS)} | {"default"}
            assert value in valid, (out, slot)


def test_daily_slot_layout_per_activity(service: JevlikeService) -> None:
    for act, slots in (("emote", ("style", "target", "facial")), ("stay", ()), ("pick_up", ("target",))):
        out = _daily(service, activities=[act])
        assert out["activity"] == act or (act != "stay" and out["activity"] == "stay")
        if out["activity"] == act:
            assert tuple(out["slots"]) == slots


def test_daily_single_candidate_skips_inference(service: JevlikeService) -> None:
    """ground_items 1개 pick_up: 활동 패스(pick_up vs stay) 1 + 대상 패스 0 = 총 1패스."""
    for seed in range(50):
        out = _daily(service, activities=["pick_up"], seed=seed)
        if out["activity"] == "pick_up":
            assert out["slots"] == {"target": "Item_7F3A"} and out["passes"] == 1
            return
    raise AssertionError("pick_up 이 50회 동안 한 번도 안 나옴")


def test_daily_max_four_passes(service: JevlikeService) -> None:
    assert max(_daily(service, seed=s)["passes"] for s in range(200)) <= 4


def test_daily_empty_pool_slot_is_default(service: JevlikeService) -> None:
    outs = [_daily(service, activities=["rest"], pools={}, seed=s) for s in range(50)]
    rests = [o for o in outs if o["activity"] == "rest"]
    assert rests and all(o["slots"] == {"target": "default"} for o in rests)


def test_daily_stay_always_present(service: JevlikeService) -> None:
    assert _daily(service, activities=[])["activity"] == "stay"
    assert _daily(service, activities=["unknown_act"])["activity"] == "stay"


def test_daily_seed_reproducible(service: JevlikeService) -> None:
    assert [_daily(service, seed=42) for _ in range(3)] == [_daily(service, seed=42)] * 3


def test_daily_exception_falls_back_to_stay(service: JevlikeService, monkeypatch: pytest.MonkeyPatch) -> None:
    def boom(*_a, **_k):
        raise RuntimeError("boom")

    monkeypatch.setattr(service, "_evaluate_daily", boom)
    assert service.evaluate_daily(METRICS, ALL_DAILY, POOLS) == {
        "activity": "stay",
        "slots": {},
        "confidence": 0.0,
        "passes": 0,
    }


def test_daily_repeat_penalty(service: JevlikeService) -> None:
    from app.services.jev_service import _DailyCtx, activity_logit

    fresh = _DailyCtx(dict(METRICS, last_activity=""), PERSONA)
    repeat = _DailyCtx(dict(METRICS, last_activity="wander"), PERSONA)
    assert activity_logit("wander", repeat) < activity_logit("wander", fresh)


def test_daily_blocks_command_only_activities(service: JevlikeService) -> None:
    for seed in range(100):
        assert _daily(service, activities=["follow", "equip", "stay"], seed=seed)["activity"] == "stay"


def test_daily_give_item_never_targets_player_or_ground(service: JevlikeService) -> None:
    pools = dict(POOLS, actors=POOLS["actors"] + [{"id": "ground", "desc": "ground"}])
    for seed in range(200):
        out = _daily(service, activities=["give_item"], pools=pools, seed=seed)
        if out["activity"] == "give_item":
            assert out["slots"]["target"] not in ("Player", "ground")
            assert out["slots"]["item"] != "Seal"  # 퀘스트템 제외


def test_daily_emote_style_candidates_are_media_pool() -> None:
    from app.services.jev_service import slot_candidates

    assert [c[0] for c in slot_candidates("emote", "style", POOLS)] == POOLS["media"]


def test_daily_latency_budget(service: JevlikeService) -> None:
    t0 = time.perf_counter()
    for s in range(100):
        _daily(service, seed=s)
    avg_ms = (time.perf_counter() - t0) * 1000 / 100
    assert avg_ms < 2.0, f"평균 {avg_ms:.2f}ms — daily 2ms 예산 초과"


def test_handle_jev_query_daily_and_default_combat(monkeypatch: pytest.MonkeyPatch, service: JevlikeService) -> None:
    from app import main

    monkeypatch.setattr(main, "get_jev_service", lambda: service)
    monkeypatch.setattr(main, "load_persona", lambda _id: PERSONA)
    daily = MessageEnvelope(
        msg_id="d1",
        auth_token="x",
        type=EEnvelopeType.JEV_QUERY,
        payload={"npc_id": "Guard", "generation": 7, "domain": "daily", "metrics": METRICS,
                 "activities": ALL_DAILY, "pools": POOLS},
    )
    p = json.loads(main._handle_jev_query(daily))["payload"]
    assert p["domain"] == "daily" and p["generation"] == 7 and p["activity"] in ALL_DAILY
    assert {"slots", "confidence", "passes"} <= set(p)

    # domain 누락 → combat 경로(stance·승수)
    combat = MessageEnvelope(msg_id="c1", auth_token="x", type=EEnvelopeType.JEV_QUERY, payload={"npc_id": "Guard"})
    p = json.loads(main._handle_jev_query(combat))["payload"]
    assert REQUIRED_KEYS <= set(p) and "activity" not in p
    assert JevQueryPayload(npc_id="G").domain == "combat"
