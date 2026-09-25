"""sweep.py — Jev daily 재학습 조정값 스윕(라벨 재생성 없음). 설정마다 build → jevlike.train → 목표 지표.

목표(2026-09-25): 활동 정답률 ≥75% · test look_at 선택 ≤60% · stay 단독 1위 재현율 ≥40%
               · 동적 골드 모델==LLM 1위 ≥60% · 골드 look_at ≤40%.
재현율은 LLM 1위가 **단독**인 패스만 센다(동률이면 어느 쪽이든 정답이라 옵션 순서가 결과를 왜곡).

실행(CognitiveEngine 에서): ../../.venv/Scripts/python.exe finetune/jev/sweep.py "sharpen=2,bmax=4,width=64,epochs=8" ...
결과는 finetune/jev/runs/sweep.tsv 에 한 줄씩 누적.
"""

import collections
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import jev_dataset as j  # noqa: E402

RUNS = HERE / "runs"
TSV = RUNS / "sweep.tsv"
TARGET = {"acc": 0.75, "look": 0.60, "stay": 0.40, "gold": 0.60, "glook": 0.40}


def metrics(ckpt: Path) -> dict:
    svc = j.JevlikeService(checkpoint_path=str(ckpt))
    assert svc.model_loaded, ckpt
    test = [r for r in j._read(j.DATA / "test.jsonl") if r["slot"] == "activity"]
    gold = j._read(j.DATA / "gold_scenarios.jsonl")

    def act(r, i):
        return r["options"][i].split("|", 1)[0]

    hit = look = 0
    rec = collections.defaultdict(lambda: [0, 0])
    for r in test:
        p = svc._model_probs(r["context"], r["options"])
        pred = max(range(len(p)), key=p.__getitem__)
        hit += pred in r["best"]
        look += act(r, pred) == "look_at"
        if len(r["best"]) == 1:
            b = r["best"][0]
            rec[act(r, b)][0] += 1
            rec[act(r, b)][1] += pred == b
    picks = [j._model_pick(svc, s)["activity"] for s in gold]
    gagree = sum(m == max(s["llm_weights"], key=s["llm_weights"].get) for m, s in zip(picks, gold))
    stay_n, stay_h = rec["stay"]
    return {
        "acc": hit / len(test),
        "look": look / len(test),
        "stay": stay_h / max(stay_n, 1),
        "gold": gagree / len(gold),
        "glook": picks.count("look_at") / len(gold),
    }


def passed(m: dict) -> bool:
    return (m["acc"] >= TARGET["acc"] and m["look"] <= TARGET["look"] and m["stay"] >= TARGET["stay"]
            and m["gold"] >= TARGET["gold"] and m["glook"] <= TARGET["glook"])


def run(spec: str) -> dict:
    cfg = dict(kv.split("=") for kv in spec.split(","))
    name = "sw_" + "_".join(f"{k}{v}" for k, v in cfg.items())
    py = sys.executable
    subprocess.run([py, str(HERE / "jev_dataset.py"), "build", "--sharpen", cfg.get("sharpen", "1"),
                    "--balance-max", cfg.get("bmax", "4")], check=True, capture_output=True)
    ckpt = RUNS / f"{name}.pt"
    subprocess.run([py, "-m", "jevlike.train", str(j.DATA / "soft/train.jsonl"), "--validation",
                    str(j.DATA / "soft/validation.jsonl"), "--output", str(ckpt), "--encoder", "tiny",
                    "--width", cfg.get("width", "64"), "--rank", cfg.get("width", "64"), "--context-tokens", "320",
                    "--option-tokens", "64", "--epochs", cfg.get("epochs", "8"),
                    "--learning-rate", cfg.get("lr", "0.002"), "--seed", cfg.get("seed", "7"), "--device", "cuda"],
                   check=True, capture_output=True, cwd=str(HERE))
    m = metrics(ckpt)
    line = f"{name}\t" + "\t".join(f"{m[k]:.3f}" for k in TARGET) + f"\t{'PASS' if passed(m) else ''}"
    RUNS.mkdir(exist_ok=True)
    if not TSV.exists():
        TSV.write_text("name\t" + "\t".join(TARGET) + "\tresult\n", encoding="utf-8")
    with open(TSV, "a", encoding="utf-8") as f:
        f.write(line + "\n")
    print(line, flush=True)
    return m


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    for spec in sys.argv[1:]:
        run(spec)
