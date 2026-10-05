"""Multi-LLM Federated Orchestrator — Gemini/agy(Master) → Claude(Dev) → SoL-Pi / Codex(QA) → Gemini/agy(승인)

루프는 이 스크립트가 결정론적으로 돌리고, 각 LLM 은 headless CLI 로 한 번씩만 부른다.
설계 문서: docs/SPEC_crewai_multi_agent.md

실행:
  python tools/federated_orchestrator.py --spec docs/SPEC_xxx.md [--worktree] [--codex] [--note "추가 지시"]
  python tools/federated_orchestrator.py --selftest

  --worktree : C:\\github\\<repo>_wt_<task_id> 에 git worktree(브랜치 fed/<task_id>)를 만들어 격리 작업.
               생략 시 현재 작업 트리를 쓰되, 미커밋 변경이 있으면 거부한다(남의 변경이 Dev 결과에 섞임).
  --codex    : Codex 정적 리뷰 활성화. WARNING 이면 Codex 가 직접 고치고, REJECTED 면 Claude 에 반려.
  --review   : 리뷰어 선택 none|codex|gemini. gemini 는 agy(MASTER_MODEL) 가 읽기 전용으로 리뷰 —
               WARNING 은 고치지 않고 통과로 두고, REJECTED 면 Claude 에 반려. --codex 는 --review codex 와 같다.

종료 코드: 0 통과·승인 / 1 준비 실패 / 2 재작업 한도 초과(사람 에스컬레이션) / 3 Master 반려
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path
from typing import List, Literal, Optional

from pydantic import BaseModel

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from sol_pi import is_editor_running  # noqa: E402

MAX_REWORK = 2  # QA 반려 후 Claude 재작업 최대 횟수
AGY = Path.home() / ".gemini" / "bin" / "agy.exe"  # Antigravity CLI (Master = Gemini)
MASTER_MODEL = "gemini-3.1-pro-high"  # 미지정 시 agy 기본(Flash)으로 계획·승인 — 판정 역할이라 Pro 고정
VERIFY_TARGET = {"UE5_CPP": "all", "PYTHON_BACKEND": "python", "ASSET_3D": "all"}


class TaskSpecification(BaseModel):
    """Master(Gemini) → Dev(Claude) 전달 명세"""

    task_id: str
    target_domain: Literal["UE5_CPP", "PYTHON_BACKEND", "ASSET_3D"]
    goal: str
    target_files: List[str]
    rule_files: List[str]
    interface_contract: str


class DevExecutionReport(BaseModel):
    """Dev(Claude) → QA 전달 결과. 수정 파일·diff 는 LLM 자기보고가 아니라 git 에서 직접 뽑는다."""

    task_id: str
    modified_files: List[str]
    summary_of_changes: str
    git_diff_stat: str


class QAReceipt(BaseModel):
    """QA → Master(Gemini) 전달 영수증"""

    task_id: str
    is_passing: bool
    review_status: Literal["PASSED", "WARNING", "REJECTED", "SKIPPED"]
    build_status: Literal["SUCCESS", "FAILED"]
    test_summary: str
    receipt_summary: str
    error_slices: Optional[List[str]] = None


def run(cmd: List[str], cwd: Path, stdin: str = "", timeout: int = 1800) -> subprocess.CompletedProcess:
    """외부 CLI 실행. codex 는 Windows 에서 .CMD 래퍼라 여러 줄 인자가 깨지므로 프롬프트를 stdin 으로 넘긴다."""
    exe = shutil.which(cmd[0])
    if not exe:
        raise SystemExit(f"[준비 실패] '{cmd[0]}' CLI 를 PATH 에서 찾지 못함")
    return subprocess.run(
        [exe, *cmd[1:]],
        cwd=cwd,
        input=stdin,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


def extract_json(text: str) -> dict:
    """LLM 응답에서 첫 JSON 객체를 꺼낸다(```json 펜스·앞뒤 잡담 허용)."""
    m = re.search(r"\{.*\}", text, re.S)
    if not m:
        raise ValueError(f"JSON 없음: {text[:200]}")
    return json.loads(m.group(0))


def git(ws: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=ws, capture_output=True, text=True, encoding="utf-8", errors="replace"
    ).stdout


# ───────────────────────── 작업 공간 ─────────────────────────


def prepare_workspace(task_id: str, use_worktree: bool) -> Path:
    if use_worktree:
        ws = ROOT.parent / f"{ROOT.name}_wt_{task_id}"
        res = subprocess.run(
            ["git", "worktree", "add", "-b", f"fed/{task_id}", str(ws), "HEAD"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if res.returncode != 0:
            raise SystemExit(f"[준비 실패] worktree 생성 실패: {res.stderr.strip()}")
        print(f"[작업공간] worktree {ws} (브랜치 fed/{task_id}) — 첫 C++ 빌드는 전체 컴파일이라 오래 걸린다")
        return ws
    if git(ROOT, "status", "--porcelain").strip():
        raise SystemExit(
            "[준비 실패] 현재 작업 트리에 미커밋 변경이 있음 — 다른 에이전트 변경이 섞이므로 --worktree 로 실행할 것"
        )
    return ROOT


# ───────────────────────── Master (Gemini) ─────────────────────────


def gemini(prompt: str, ws: Path, schema: Optional[dict] = None) -> dict:
    """agy 결과 JSON 전체를 돌려준다. schema 를 주면 결과는 `structured_output`, 아니면 `response`(텍스트)."""
    # Gemini CLI 개인 OAuth 는 IneligibleTierError 로 막혀 Antigravity CLI(agy) 를 쓴다.
    # agy.exe 는 .CMD 래퍼가 아니라 여러 줄 인자가 안전하다.
    # plan 모드: 계획서는 agy 자체 brain 폴더에만 쓰고 저장소는 수정하지 않는다(Master 는 판단만).
    # --add-dir 없으면 cwd 가 agy 작업공간으로 안 잡혀 ViewFile 까지 거부되고 빈 응답으로 끝난다(실측).
    cmd = [
        str(AGY),
        "-p",
        prompt,
        "--output-format",
        "json",
        "--mode",
        "plan",
        "--model",
        MASTER_MODEL,
        "--add-dir",
        str(ws),
    ]
    cmd += ["--print-timeout", "600s"]
    if schema:
        cmd += ["--json-schema", json.dumps(schema)]
    res = run(cmd, ws, timeout=700)
    try:
        out = json.loads(res.stdout)
    except json.JSONDecodeError:
        raise SystemExit(f"[Master 실패] agy 종료코드 {res.returncode}: {(res.stderr or res.stdout).strip()[:500]}")
    if out.get("status") != "SUCCESS":
        raise SystemExit(f"[Master 실패] agy status={out.get('status')}: {str(out)[:500]}")
    return out


def master_plan(spec_rel: str, task_id: str, note: str, ws: Path) -> TaskSpecification:
    # SPEC 본문을 인자로 넣으면 Windows 명령줄 32K 제한에 걸릴 수 있어 경로만 주고 직접 읽게 한다.
    # print 모드는 셸 명령 승인을 못 받아 RunCommand 가 거부되면 빈 응답으로 끝난다 → 파일 목록을 직접 주고 셸 금지.
    rules = ", ".join(p.relative_to(ws).as_posix() for p in sorted((ws / ".agents" / "rules").glob("*.md")))
    prompt = f"""너는 이 저장소의 Master AI(총괄 아키텍트)다. 셸/터미널 명령은 실행하지 말고 파일 보기 도구로만 읽어라.
AGENTS.md 와 필요한 규칙 파일({rules})을 읽고, SPEC 파일 `{spec_rel}` 에서
지금 구현할 작업 하나를 골라 TaskSpecification JSON 하나만 출력하라(설명·마크다운 금지).
고르기 전에 후보의 대상 파일을 실제로 읽어 이미 구현돼 있으면 제외하라(SPEC 체크박스는 늦게 갱신될 수 있음).

필드: task_id="{task_id}", target_domain ∈ UE5_CPP|PYTHON_BACKEND|ASSET_3D, goal(한 문장),
target_files(저장소 상대경로 목록), rule_files(.agents/rules/ 중 필요한 것만), interface_contract(시그니처·JSON 스키마, 없으면 "").

[추가 지시] {note or "없음"}"""
    out = gemini(prompt, ws, TaskSpecification.model_json_schema())
    data = out.get("structured_output")
    if not data:
        try:
            data = extract_json(out.get("response", ""))
        except ValueError:
            raise SystemExit(f"[Master 실패] TaskSpecification 없음 — denied_actions={out.get('denied_actions')}")
    return TaskSpecification(**data)


def write_diff_file(ws: Path) -> str:
    """agy 는 plan 모드라 셸(git diff)이 막힌다 — diff 를 작업공간 Saved/(ignore) 에 써 두고 경로를 돌려준다.
    프롬프트에 diff 를 직접 넣으면 Windows 명령줄 32K 제한에 걸릴 수 있다."""
    git(ws, "add", "-N", ".")
    diff_path = ws / "Saved" / "fed_review.diff"
    diff_path.parent.mkdir(parents=True, exist_ok=True)
    diff_path.write_text(git(ws, "diff"), encoding="utf-8")
    return diff_path.relative_to(ws).as_posix()


def master_approve(spec: TaskSpecification, receipt: QAReceipt, diff_stat: str, ws: Path) -> tuple[bool, str]:
    # "git diff 를 직접 읽어라" 라고 하면 셸이 막힌 agy 가 RunCommand 를 시도하다 거부돼 빈 응답으로 끝난다(실측).
    prompt = f"""너는 Master AI 다. Dev 결과가 목표를 충족하는지 최종 판정하라.
셸·git 명령은 쓰지 말고 파일 보기 도구로만 읽어라. 변경 diff 는 `{write_diff_file(ws)}` 에 있다.
첫 줄은 APPROVED 또는 REJECTED 한 단어, 둘째 줄부터 근거 한두 줄.

[목표] {spec.goal}
[QA 영수증] {receipt.model_dump_json()}
[diff stat]
{diff_stat}"""
    out = gemini(prompt, ws).get("response", "").strip()
    if not out:
        raise SystemExit("[Master 실패] 승인 응답이 비었다 — 반려가 아니라 호출 실패. 작업공간 변경은 그대로 남아 있다.")
    return out.upper().lstrip("*`# ").startswith("APPROVED"), out


# ───────────────────────── Dev (Claude) ─────────────────────────


def claude_dev(spec: TaskSpecification, feedback: str, ws: Path) -> str:
    prompt = f"""너는 Dev AI 다. 아래 TaskSpecification 만 구현한다.
- 먼저 AGENTS.md 와 rule_files 를 읽고 따른다.
- target_files 중심으로 수정. 빌드·테스트·git commit/push 는 하지 않는다(QA 가 한다).
- 끝나면 변경 요약을 3~5줄로 답한다.

TaskSpecification:
{spec.model_dump_json(indent=2)}
{f"[이전 QA 반려 사유 — 이것부터 고칠 것]{chr(10)}{feedback}" if feedback else ""}"""
    res = run(
        [
            "claude",
            "-p",
            "--output-format",
            "json",
            "--permission-mode",
            "acceptEdits",
            "--disallowedTools",
            "Bash(git commit:*)",
            "Bash(git push:*)",
        ],
        ws,
        stdin=prompt,
    )
    try:
        out = json.loads(res.stdout)
    except json.JSONDecodeError:
        return f"(claude 출력 파싱 실패, 종료코드 {res.returncode}) {res.stderr.strip()[:300]}"
    return out.get("result", "")


def dev_report(spec: TaskSpecification, summary: str, ws: Path) -> DevExecutionReport:
    files = [ln[3:] for ln in git(ws, "status", "--porcelain").splitlines()]
    git(ws, "add", "-N", ".")  # 신규 파일도 diff --stat 에 보이도록 intent-to-add
    return DevExecutionReport(
        task_id=spec.task_id, modified_files=files, summary_of_changes=summary, git_diff_stat=git(ws, "diff", "--stat")
    )


# ───────────────────────── QA (Codex + SoL-Pi) ─────────────────────────


def codex(prompt: str, ws: Path, sandbox: str) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        last = Path(tmp) / "last.txt"
        run(["codex", "exec", "-C", str(ws), "-s", sandbox, "-o", str(last), "-"], ws, stdin=prompt)
        return last.read_text(encoding="utf-8") if last.exists() else ""


def codex_review(ws: Path) -> tuple[str, str]:
    out = codex(
        """git diff 를 리뷰하라. 기준: AGENTS.md 3대 원칙, .agents/rules/ue5_cpp.md(널 가드, BB 단일 진입점,
EAction 4곳), .agents/rules/python_backend.md(Envelope 3곳 동시 수정, 타입 힌트, print 금지).
첫 줄은 PASSED / WARNING / REJECTED 한 단어. WARNING = 동작은 맞으나 규칙 위반·품질 문제, REJECTED = 버그·계약 위반.
둘째 줄부터 문제 목록(파일:줄 — 내용).""",
        ws,
        "read-only",
    ).strip()
    status = out.split(maxsplit=1)[0].upper() if out else "REJECTED"
    return (status if status in ("PASSED", "WARNING", "REJECTED") else "REJECTED"), out


REVIEW_CRITERIA = """기준: AGENTS.md 3대 원칙, .agents/rules/ue5_cpp.md(널 가드, BB 단일 진입점,
EAction 4곳), .agents/rules/python_backend.md(Envelope 3곳 동시 수정, 타입 힌트, print 금지).
첫 줄은 PASSED / WARNING / REJECTED 한 단어. WARNING = 동작은 맞으나 규칙 위반·품질 문제, REJECTED = 버그·계약 위반.
둘째 줄부터 문제 목록(파일:줄 — 내용)."""


def gemini_review(ws: Path) -> tuple[str, str]:
    """Gemini(agy) 읽기 전용 리뷰 — diff 는 파일로 넘긴다(write_diff_file)."""
    prompt = f"""너는 QA 리뷰어다. 셸·git 명령은 쓰지 말고 파일 보기 도구로만 읽어라.
변경 diff 는 `{write_diff_file(ws)}` 에 있다. 필요하면 원본 파일도 읽어라.
{REVIEW_CRITERIA}"""
    out = gemini(prompt, ws).get("response", "").strip()
    status = out.split(maxsplit=1)[0].upper().strip("*`:") if out else ""
    # 빈 응답은 반려가 아니라 리뷰 실패 — 연결 문제로 Dev 를 무한 반려하지 않게 멈춘다.
    if status not in ("PASSED", "WARNING", "REJECTED"):
        raise SystemExit(f"[QA 실패] Gemini 리뷰 응답을 해석하지 못함: {out[:300]!r}")
    return status, out


def sol_pi_verify(target: str, ws: Path) -> tuple[bool, str]:
    # 작업 공간 안의 sol_pi.py 를 돌려야 그 트리가 검증 대상이 된다(sol_pi 는 자기 파일 위치로 루트를 잡음).
    res = subprocess.run(
        [sys.executable, str(ws / "tools" / "sol_pi.py"), "verify", target],
        cwd=ws,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return res.returncode == 0, res.stdout.strip()


def qa(spec: TaskSpecification, ws: Path, reviewer: str) -> QAReceipt:
    review, review_text = "SKIPPED", ""
    if reviewer == "gemini":
        review, review_text = gemini_review(ws)
        print(f"[QA] Gemini 리뷰: {review}")
        if review != "PASSED":
            print(review_text)
    elif reviewer == "codex":
        review, review_text = codex_review(ws)
        print(f"[QA] Codex 리뷰: {review}")
        if review == "WARNING":
            codex(
                f"다음 리뷰 경고를 코드에서 고쳐라. 동작은 바꾸지 말고 경고만 해소하라. git commit 금지.\n\n{review_text}",
                ws,
                "workspace-write",
            )
            print("[QA] Codex 가 경고 수정 완료 — 검증으로 진행")
    if review == "REJECTED":
        return QAReceipt(
            task_id=spec.task_id,
            is_passing=False,
            review_status="REJECTED",
            build_status="FAILED",
            test_summary="리뷰 반려로 빌드 생략",
            receipt_summary=f"{reviewer} REJECTED",
            error_slices=[review_text],
        )

    ok, out = sol_pi_verify(VERIFY_TARGET[spec.target_domain], ws)
    tail = out.splitlines()[-12:]
    return QAReceipt(
        task_id=spec.task_id,
        is_passing=ok,
        review_status=review,
        build_status="SUCCESS" if ok else "FAILED",
        test_summary=" / ".join(tail[-3:]),
        receipt_summary="\n".join(tail),
        error_slices=None if ok else tail,
    )


# ───────────────────────── 메인 루프 ─────────────────────────


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--spec")
    ap.add_argument("--note", default="")
    ap.add_argument("--worktree", action="store_true")
    ap.add_argument("--codex", action="store_true")
    ap.add_argument("--review", choices=["none", "codex", "gemini"], default="none")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument(
        "--plan-only", action="store_true", help="Master 의 TaskSpecification 만 출력하고 끝(Dev·QA 미실행)"
    )
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.spec:
        ap.error("--spec 필요")

    spec_path = (ROOT / a.spec).resolve()
    if not spec_path.exists():
        ap.error(f"SPEC 없음: {spec_path}")
    spec_rel = spec_path.relative_to(ROOT).as_posix()
    task_id = f"{spec_path.stem.removeprefix('SPEC_')}-{datetime.now():%m%d%H%M}"
    if a.plan_only:
        print(master_plan(spec_rel, task_id, a.note, ROOT).model_dump_json(indent=2))
        return 0

    ws = prepare_workspace(task_id, a.worktree)
    if not (ws / spec_rel).exists():
        print(f"[준비 실패] worktree 에 {spec_rel} 없음 — worktree 는 HEAD 기준이라 SPEC 을 먼저 커밋할 것")
        return 1
    spec = master_plan(spec_rel, task_id, a.note, ws)
    print(f"[Master] {spec.target_domain} — {spec.goal}\n  files: {spec.target_files}")
    if spec.target_domain == "UE5_CPP" and ws == ROOT and is_editor_running():
        print("[준비 실패] 에디터 실행 중 — 같은 프로젝트 Build.bat 은 Live Coding 과 충돌. 에디터를 닫거나 --worktree")
        return 1

    feedback = ""
    for attempt in range(MAX_REWORK + 1):
        print(f"[Dev] Claude 구현 (시도 {attempt + 1}/{MAX_REWORK + 1})")
        report = dev_report(spec, claude_dev(spec, feedback, ws), ws)
        if not report.modified_files:
            feedback = "변경된 파일이 없다. TaskSpecification 을 실제로 구현하라."
            continue
        receipt = qa(spec, ws, "codex" if a.codex else a.review)
        print(f"[QA] {'통과' if receipt.is_passing else '실패'}\n{receipt.receipt_summary}")
        if receipt.is_passing:
            break
        feedback = "\n".join(receipt.error_slices or [])
    else:
        print(f"[에스컬레이션] 재작업 {MAX_REWORK}회 초과 — 사람 확인 필요. 작업공간: {ws}")
        return 2

    approved, verdict = master_approve(spec, receipt, report.git_diff_stat, ws)
    print(f"[Master] {verdict}")
    if not approved:
        return 3
    if ws != ROOT:
        git(ws, "add", "-A")
        git(ws, "commit", "-q", "-m", f"feat: {spec.goal[:50]}")
        print(f"[완료] 브랜치 fed/{task_id} 에 커밋. 병합·worktree 정리는 사람 몫: git worktree remove {ws}")
    print(f"[Memo Done 제안] - [x] **{spec.goal}** ({datetime.now():%Y-%m-%d}, federated) — {receipt.test_summary}")
    return 0


def selftest() -> int:
    assert extract_json('잡담\n```json\n{"a": 1}\n```') == {"a": 1}
    spec = TaskSpecification(
        task_id="t", target_domain="PYTHON_BACKEND", goal="g", target_files=[], rule_files=[], interface_contract=""
    )
    assert VERIFY_TARGET[spec.target_domain] == "python"
    try:
        extract_json("json 없음")
        raise AssertionError("JSON 없는 응답이 통과함")
    except ValueError:
        pass
    print("selftest ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
