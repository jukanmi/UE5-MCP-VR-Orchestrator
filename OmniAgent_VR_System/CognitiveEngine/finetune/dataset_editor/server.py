"""파인튜닝 시드 데이터셋 편집기 — 독립 실행(Ollama/UE5/메인 서버 무관).

scenarios_seed_draft.yaml 을 표 형태로 조회·추가·수정·삭제. 저장 시 실제
rules.validate_and_clamp_action 으로 즉시 검증(필수 파라미터 누락 등 즉석 확인,
Equip 시드 버그 같은 문제를 작성 시점에 잡기 위함, 2026-07-27).

실행:
    python finetune/dataset_editor/server.py
    → http://127.0.0.1:8010
"""

import os
import sys

import yaml
from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

_HERE = os.path.dirname(os.path.abspath(__file__))
_ENGINE_ROOT = os.path.abspath(os.path.join(_HERE, "../.."))  # CognitiveEngine
sys.path.insert(0, _ENGINE_ROOT)

from app.agents.subgraphs.rules import validate_and_clamp_action  # noqa: E402
from app.schemas.actions import DIALOGUE_ACTION_FIELD_MAP, GameAction  # noqa: E402

SEED_PATH = os.path.join(_HERE, "..", "data", "synth", "scenarios_seed_draft.yaml")
CORE_NPCS = ["Elara", "James", "Skadi", "Moca", "Guard"]

app = FastAPI(title="Finetune Dataset Editor")


# ── yaml 입출력 ──────────────────────────────────────────────────────────────
def _load() -> list[dict]:
    with open(SEED_PATH, encoding="utf-8") as fp:
        return yaml.safe_load(fp) or []


def _save(seeds: list[dict]) -> None:
    with open(SEED_PATH, "w", encoding="utf-8") as fp:
        yaml.safe_dump(seeds, fp, allow_unicode=True, sort_keys=False)


# ── 검증(실제 rules.py 경로 재사용, generate_stage1.py::rules_gate 와 동형) ──
def _gate(actions: list[dict], valid_targets: list[str]) -> tuple[list[dict] | None, str]:
    field_to_param = {f: p for p, f in DIALOGUE_ACTION_FIELD_MAP}
    runtime = set(valid_targets) if valid_targets else None
    survived = []
    for a in actions:
        if "type" not in a:
            return None, "액션에 'type' 키 없음"
        params = {}
        for field in ("target", "loc", "item", "style"):
            v = str(a.get(field, "") or "")
            if v:
                params[field_to_param[field]] = v
        ga = GameAction(ActionType=a["type"], FacialState="Neutral", Parameters=params)
        try:
            validated, corr = validate_and_clamp_action(ga, runtime)
        except Exception as e:  # noqa: BLE001
            return None, f"검증 예외: {e}"
        if validated is None:
            return None, f"{a['type']}: {'; '.join(corr) if corr else '필수 파라미터 누락/유효하지 않은 target'}"
        back = {"type": validated.ActionType}
        for param, field in DIALOGUE_ACTION_FIELD_MAP:
            v = validated.Parameters.get(param, "")
            if v:
                back[field] = v
        survived.append(back)
    return survived, ""


class SeedIn(BaseModel):
    id: str
    category: str
    npc: str
    utterance: str
    situation: dict = {}
    gold: dict = {}


@app.get("/api/seeds")
def list_seeds(npc: str = "", category: str = "", q: str = ""):
    seeds = _load()
    out = []
    for s in seeds:
        if npc and s.get("npc") != npc:
            continue
        if category and s.get("category") != category:
            continue
        if q and q.lower() not in (s.get("utterance", "") + s.get("id", "")).lower():
            continue
        actions = (s.get("gold", {}) or {}).get("actions") or []
        valid_targets = (s.get("situation", {}) or {}).get("valid_targets") or []
        gated, reason = _gate(actions, valid_targets)
        out.append(
            {
                "id": s.get("id"),
                "category": s.get("category"),
                "npc": s.get("npc"),
                "utterance": s.get("utterance"),
                "sentiment": (s.get("situation", {}) or {}).get("sentiment", "Neutral (Score: 0)"),
                "speech_hint": (s.get("gold", {}) or {}).get("speech_hint", ""),
                "actions_summary": ", ".join(
                    f"{a.get('type')}"
                    + (f"({a.get('target') or a.get('item') or ''})" if (a.get("target") or a.get("item")) else "")
                    for a in actions
                )
                or "(무동작)",
                "valid": gated is not None,
                "invalid_reason": reason,
            }
        )
    return out


@app.get("/api/coverage")
def coverage():
    seeds = _load()
    total = {}
    per_core = {npc: {} for npc in CORE_NPCS}
    for s in seeds:
        npc = s.get("npc", "")
        for a in (s.get("gold", {}) or {}).get("actions") or []:
            t = a.get("type", "?")
            total[t] = total.get(t, 0) + 1
            if npc in per_core:
                per_core[npc][t] = per_core[npc].get(t, 0) + 1
    return {"total": total, "per_core": per_core, "seed_count": len(seeds)}


@app.get("/api/seeds/{seed_id}")
def get_seed(seed_id: str):
    for s in _load():
        if s.get("id") == seed_id:
            return s
    raise HTTPException(404, "not found")


@app.post("/api/validate")
def validate_only(body: SeedIn):
    actions = (body.gold or {}).get("actions") or []
    valid_targets = (body.situation or {}).get("valid_targets") or []
    gated, reason = _gate(actions, valid_targets)
    return {"valid": gated is not None, "reason": reason}


@app.post("/api/seeds")
def create_seed(body: SeedIn):
    seeds = _load()
    if any(s.get("id") == body.id for s in seeds):
        raise HTTPException(400, f"id '{body.id}' 이미 존재")
    seeds.append(body.model_dump())
    _save(seeds)
    return {"ok": True}


@app.put("/api/seeds/{seed_id}")
def update_seed(seed_id: str, body: SeedIn):
    seeds = _load()
    for i, s in enumerate(seeds):
        if s.get("id") == seed_id:
            seeds[i] = body.model_dump()
            _save(seeds)
            return {"ok": True}
    raise HTTPException(404, "not found")


@app.delete("/api/seeds/{seed_id}")
def delete_seed(seed_id: str):
    seeds = _load()
    new_seeds = [s for s in seeds if s.get("id") != seed_id]
    if len(new_seeds) == len(seeds):
        raise HTTPException(404, "not found")
    _save(new_seeds)
    return {"ok": True}


@app.get("/", response_class=HTMLResponse)
def index():
    with open(os.path.join(_HERE, "static", "index.html"), encoding="utf-8") as fp:
        return fp.read()


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="127.0.0.1", port=8010)
