"""PickUp 대상 아이템 경로 — 필수 파라미터 완화와 바닥 아이템 프롬프트 조각."""

from types import SimpleNamespace

from app.agents.interface_input import _format_nearby_items
from app.agents.subgraphs.rules import _missing_required_group
from app.schemas.actions import GameAction


def test_pickup_accepts_item_target_id_only():
    action = GameAction(ActionType="PickUp", Parameters={"target_id": "Item_7F3A"})
    assert _missing_required_group(action) is None


def test_pickup_still_accepts_legacy_target_loc():
    action = GameAction(ActionType="PickUp", Parameters={"target_loc": "X=1 Y=2 Z=0"})
    assert _missing_required_group(action) is None


def test_pickup_without_target_is_rejected():
    action = GameAction(ActionType="PickUp", Parameters={})
    assert _missing_required_group(action) is not None


def test_nearby_items_renders_id_kind_and_distance():
    ctx = SimpleNamespace(nearby_items=[{"id": "Item_7F3A", "template_id": "Herb", "dist_m": 2.0}])
    text = _format_nearby_items(ctx)
    assert "Item_7F3A (Herb, 2.0m away)" in text
    assert "PickUp" in text


def test_nearby_items_empty_is_blank():
    assert _format_nearby_items(SimpleNamespace(nearby_items=None)) == ""
    assert _format_nearby_items(SimpleNamespace()) == ""
