"""Jev 전투 성격 돌파 항(apply_personality) — 중립·1:1 무효과, 포위 시 성격에 단조, 빈사 시 억제."""

from app.services.jev_service import apply_personality, heuristic_probs

SURROUNDED = {"hp_pct": 1.0, "distance_m": 3.0, "enemy_count": 2, "is_flanked": True}


def _p_aggr(metrics: dict) -> float:
    return apply_personality(heuristic_probs(metrics), metrics)[0]


def test_neutral_or_missing_trait_changes_nothing():
    base = heuristic_probs(SURROUNDED)
    assert apply_personality(base, SURROUNDED) == base
    assert apply_personality(base, {**SURROUNDED, "aggression": 0.5, "bravery": 0.5}) == base


def test_one_on_one_has_no_pressure_so_no_effect():
    duel = {"hp_pct": 1.0, "distance_m": 3.0, "enemy_count": 1, "is_flanked": False, "aggression": 1.0, "bravery": 1.0}
    assert apply_personality(heuristic_probs(duel), duel) == heuristic_probs(duel)


def test_breakthrough_is_monotonic_in_trait_when_surrounded():
    probs = [_p_aggr({**SURROUNDED, "aggression": t, "bravery": t}) for t in (0.0, 0.5, 1.0)]
    assert probs[0] < probs[1] < probs[2]


def test_dying_npc_gets_no_breakthrough():
    dying = {**SURROUNDED, "hp_pct": 0.1, "aggression": 1.0, "bravery": 1.0}
    assert _p_aggr(dying) < 0.05
