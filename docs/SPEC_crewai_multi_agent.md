# SPEC: 멀티 LLM 연합(Gemini Master - Claude Dev - Codex QA) 자율형 다중 에이전트 오케스트레이션 시스템

> **상태**: 기획 승인 및 역할 재정의 완료 (2026-09-19)  
> **문서 버전**: v2.0 (멀티 LLM 플랫폼 쿼터 분산 연합 모델)  
> **대상 플랫폼**: Unreal Engine 5.5 (C++ / Chaos / StateTree) + Python 3.10+ (FastAPI / LangGraph)  
> **핵심 하네스**: NVIDIA SoL-Pi (Action Fusion & Phase Detection)  
> **연합 모델**: **Gemini (Master PM) + Claude Code (Dev) + Codex CLI (QA)**

---

## 1. 개요 및 배경 (Overview & Objectives)

### 1.1 배경
단일 LLM 플랫폼의 유료 API를 무제한으로 호출하는 기존 방식(CrewAI 등)은 구독제 할당량(Rate Limit/Quota) 환경에서 20~50회의 내부 에이전트 핑퐁으로 인해 **시간당/일일 할당량이 급격히 고갈(Quota Lock)**되는 치명적인 문제가 발생한다.

### 1.2 멀티 LLM 쿼터 분산 연합(Multi-LLM Federation) 솔루션
본 시스템은 각기 다른 빅테크의 최정상 AI 플랫폼 3사를 역할별로 분리 배치하여 **단일 쿼터 고갈을 원천 차단**하고, 각 모델의 독보적 강점만을 결합한다:

1. **Master AI (Google Gemini / Antigravity)**: 200만 토큰 대용량 컨텍스트로 프로젝트 전체 조망, 아키텍처 기획, 작업 명세서 작성 및 전체 오케스트레이션 전담 (Google 쿼터).
2. **Dev AI (Anthropic Claude Code CLI)**: 현존 최강의 C++ 및 복잡 로직 코딩 역량을 활용하여 실제 소스 코드 구현 전담 (Anthropic 쿼터).
3. **QA AI (OpenAI Codex / Copilot CLI)**: 변경된 코드의 정적 분석(코드 리뷰) 및 SoL-Pi 하네스(`sol_pi.py verify all`) 연동을 통한 컴파일/UAT 자동화 테스트 무결성 판정 전담 (OpenAI/GitHub 쿼터).

---

## 2. 에이전트 3계층 아키텍처 및 역할 정의

```
       ┌────────────────────────────────────────────────────────┐
       │        사용자 (Human Director / Lead Developer)        │
       └───────────────────────────┬────────────────────────────┘
                                   │ 자연어 요구사항
                                   ▼
       ┌────────────────────────────────────────────────────────┐
       │   Master AI: Google Gemini (Antigravity Orchestrator)  │
       │   - 1M+ 컨텍스트 조망: docs/, AGENTS.md, 레포 분석      │
       │   - 작업 명세서(TaskSpecification) 및 계약 앵커 확정   │
       │   - Claude Code 및 Codex CLI 백그라운드 프로세스 지휘   │
       └───────────────────────────┬────────────────────────────┘
                                   │ 작업 지시서 + 규정 핀포인트
                                   ▼
       ┌────────────────────────────────────────────────────────┐
       │   Dev AI: Anthropic Claude Code CLI (Implementation)   │
       │   - UE5.5 C++ (클래스/StateTree/GameplayTag) 구현      │
       │   - Python 인지 엔진 비동기 로직 구현                  │
       │   - .agents/rules/ 규칙 준수 및 Git Diff 생성          │
       └───────────────────────────┬────────────────────────────┘
                                   │ 코드 수정 완료 (Git Diff)
                                   ▼
       ┌────────────────────────────────────────────────────────┐
       │   QA AI: OpenAI Codex / Copilot CLI (Verification)     │
       │   - 1단계: 정적 코드 리뷰 (Null 가드, BB 단일진입 감사) │
       │   - 2단계: SoL-Pi 하네스(sol_pi.py verify all) 실행    │
       │   - [실패 시] 에러 슬라이스 첨부하여 Claude Code 반려  │
       │   - [성공 시] 7줄 Compact Receipt 발행 후 Gemini 보고  │
       └───────────────────────────┬────────────────────────────┘
                                   │ 최종 검증 통과 영수증
                                   ▼
       ┌────────────────────────────────────────────────────────┐
       │   Master AI: Gemini 최종 승인 및 docs/Memo.md 갱신     │
       └────────────────────────────────────────────────────────┘
```

### 2.1 Master AI: Google Gemini (총괄 아키텍트 & PM)
- **플랫폼**: Google Gemini (Antigravity IDE 환경)
- **주요 책임**:
  - 사용자 요구사항 해석 및 아키텍처 기획 (`docs/SPEC_*.md`).
  - 도메인별 작업 범위 분할 및 C++/Python 통신 규격(`MessageEnvelope`, `NPCActionKeys`) 확정.
  - Claude Code 및 Codex CLI의 비대화형(Headless) 프로세스 호출 및 전체 라이프사이클 관리.
  - 최종 검증 영수증 확인 후 [`docs/Memo.md`](file:///c:/github/UE5_MCP_VR/docs/Memo.md) `## Done` 업데이트.

### 2.2 Dev AI: Anthropic Claude Code (전문 실무 개발자)
- **플랫폼**: Claude Code CLI (`claude -p "..." --dangerously-skip-permissions`)
- **주요 책임**:
  - Master AI가 하달한 명세서와 핀포인트 규칙(`.agents/rules/ue5_cpp.md`, `python_backend.md`)만 읽고 소스 코드 직접 수정.
  - C++ `EAction` 4곳 수정, Blackboard 단일 진입점, EQS 2종 제한 원칙 엄격 준수.
  - 코드 작성 완료 후 수정 파일 목록과 변경 요약본(`DevExecutionReport`) 생성.

### 2.3 QA AI: OpenAI Codex / Copilot CLI (품질 보증 및 하네스 검증관)
- **플랫폼**: Codex CLI / GitHub Copilot CLI (`codex exec "..."` 또는 전용 파이썬 래퍼)
- **주요 책임**:
  - **정적 코드 감사**: Claude Code가 수정한 코드에 대해 메모리 릭, Null 포인터 체크 누락, Blackboard 직접 쓰기 위반 여부 감사.
  - **SoL-Pi 하네스 검증**: `python tools/sol_pi.py verify all`을 직접 구동하여 C++ UBT 빌드 $\rightarrow$ Python 51개 pytest $\rightarrow$ UE5 UAT 엔진 헤드리스 테스트 일괄 실행.
  - **피드백 루프**:
    - 컴파일/테스트 에러 발생 시: `sol_pi_raw.log`에서 발췌된 샌드위치 에러 청크 5줄을 Claude Code에게 전달하여 재작업 지시 (최대 2회).
    - 전원 통과 시: 7줄짜리 `Compact Receipt`를 발행하여 Gemini Master에게 무결성 승인 보고.

---

## 3. 프로세스 및 데이터 교환 프로토콜

### 3.1 Pydantic 데이터 모델 (엄격한 인터페이스 계약)

```python
from pydantic import BaseModel, Field
from typing import List, Optional, Literal

class TaskSpecification(BaseModel):
    """Master(Gemini) -> Dev(Claude) 전달 명세"""
    task_id: str
    target_domain: Literal["UE5_CPP", "PYTHON_BACKEND", "ASSET_3D"]
    goal: str
    target_files: List[str]
    rule_files: List[str]  # e.g., [".agents/rules/ue5_cpp.md"]
    interface_contract: str  # JSON Schema 또는 헤더 선언 시그니처

class DevExecutionReport(BaseModel):
    """Dev(Claude) -> QA(Codex) 전달 결과"""
    task_id: str
    modified_files: List[str]
    summary_of_changes: str
    git_diff_stat: str

class QAReceipt(BaseModel):
    """QA(Codex) -> Master(Gemini) 전달 영수증"""
    task_id: str
    is_passing: bool
    review_status: Literal["PASSED", "WARNING", "REJECTED"]
    build_status: Literal["SUCCESS", "FAILED"]
    test_summary: str
    receipt_summary: str
    error_slices: Optional[List[str]] = None
```

---

## 4. 실행 러너 아키텍처 (`tools/federated_orchestrator.py`)

Gemini Master가 Claude Code와 Codex CLI를 유기적으로 오케스트레이션하기 위한 파이프라인 청사진이다.

```python
"""Multi-LLM Federated Orchestrator (Gemini Master -> Claude Dev -> Codex QA)
실행: python tools/federated_orchestrator.py --spec "docs/SPEC_xxx.md"
"""

import sys
import subprocess
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

def run_claude_dev(task_prompt: str) -> str:
    """Claude Code CLI를 비대화형으로 실행하여 코드 수정 수행"""
    cmd = [
        "claude", "-p", task_prompt,
        "--dangerously-skip-permissions"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return res.stdout

def run_codex_qa(diff_context: str) -> bool:
    """Codex CLI를 통해 정적 감사 수행 및 SoL-Pi 하네스 구동"""
    # 1. 정적 코드 리뷰 (Codex / Copilot)
    # ... 정적 분석 프롬프트 실행 ...

    # 2. SoL-Pi 하네스 액션 퓨전 검증
    verify_cmd = [sys.executable, str(PROJECT_ROOT / "tools" / "sol_pi.py"), "verify", "all"]
    v_res = subprocess.run(verify_cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    
    print(v_res.stdout)
    return v_res.returncode == 0
```

---

## 5. 게임 엔진 환경 특화 안전 가드레일 (Safety Guardrails)

### 5.1 쿼터 폭망 방지 및 토큰 다이어트
- Claude Code 호출 시 프로젝트 전체 레포를 스캔하지 않도록, Gemini Master가 **수정할 파일과 필수 규칙(`.agents/rules/`)만을 지정(Progressive Disclosure)**하여 프롬프트를 주입.
- Codex QA 검증 시 수만 줄의 빌드 로그를 전달하지 않고, `sol_pi.py`가 슬라이싱한 7줄짜리 영수증만 전달.

### 5.2 2회 자가 수정 루프 제한 (Loop Breaker)
- Codex QA가 Claude Dev에게 에러 수정을 재요청하는 피드백 횟수는 **최대 2회**로 엄격히 제한.
- 2회 연속 컴파일 실패 시 Gemini Master를 거쳐 인간 사용자에게 상황을 즉시 에스컬레이션.

### 5.3 120초 Watchdog 프로세스 킬러
- UAT 엔진 테스트 또는 빌드가 물리 크래시나 무한 루프에 걸릴 경우 `sol_pi.py`의 120초 Watchdog이 프로세스를 강제 종료(`taskkill`)하여 전체 자동화 파이프라인의 행(Hang)을 방지.

---

## 6. 단계별 도입 로드맵 (Roadmap)

### Phase 1: 스펙 확정 및 역할 재정의 (완료)
- [x] Gemini(Master) - Claude Code(Dev) - Codex(QA) 3사 연합 아키텍처 SPEC 확립 (`docs/SPEC_crewai_multi_agent.md`).
- [x] `docs/Memo.md` 반영 완료.

### Phase 2: 연합 오케스트레이터 러너 구축 (`tools/federated_orchestrator.py`)
- [ ] Claude Code CLI 및 Codex CLI 비대화형 호출 래퍼 작성.
- [ ] SoL-Pi 하네스 연동 및 Pydantic 계약 모델 바인딩.

### Phase 3: 파일럿 실전 투입 및 검증
- [ ] 파일럿 과제: `docs/Memo.md` 백로그인 **`[갭 5] 방어 및 패링 (Block / Parry)`** 물리 판정 구현에 3사 연합 파이프라인 가동.
- [ ] Claude Code(C++ 작성) $\rightarrow$ Codex(SoL-Pi 검증) $\rightarrow$ Gemini(최종 승인) 실측.
