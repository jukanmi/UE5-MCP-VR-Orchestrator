# UE5-MCP-VR Orchestrator — Agent Guidelines (AGENTS.md)

이 문서는 모든 자율형 코딩 에이전트(Antigravity, Claude Code, Codex, Gemini, Cursor 등)가 공통으로 준수해야 하는 최상위 프로젝트 인덱스(Meta-README)이자 공통 규칙의 단일 진실 공급원(SSOT)입니다.
불필요한 토큰 낭비를 방지하기 위해 **점진적 정보 노출(Progressive Disclosure)** 원칙을 따르며, 세부 도메인 작업 시 하위 규칙을 선택적으로 열람하십시오.
에이전트별 전용 파일(`CLAUDE.md` 등)에는 그 에이전트에만 해당하는 내용만 두고, 공통 규칙은 이 파일에만 적습니다.

---

## 1. System Architecture (시스템 개요)
- **Client (UE5)**: 언리얼 엔진 5.5 C++ & Blueprint 기반 VR 게임. 물리(Chaos), IK, StateTree 중심의 결정론적 런타임 실행.
- **Server (Python)**: FastAPI & WebSocket 기반 비동기 인지 엔진. 로컬 SLM(Ollama) 및 정책 기반 의사결정.
- **Protocol**: UE5와 Python은 `MessageEnvelope` 규격의 JSON으로 실시간 양방향 통신. 단일 채널 `ws://127.0.0.1:8000/ws/llm` 만 사용.
- **NPC 식별**: `AgentID` (FString), `UNPCManager::RegisterNPC/GetNPCById` 로 등록/조회.
- **AI 로직**: StateTree. `SmartNPCAIController` → `STTask_PrepareNextAction` → `STTask_ExecuteSmartAction`.

---

## 2. Inviolable Core Principles (절대 위반 불가 3대 원칙)
1. **JSON 통신 규격**:
   - 모든 통신은 `MessageEnvelope`로 래핑.
   - Parameters 내부는 `snake_case`, 최상위는 `PascalCase`. 리터럴 문자열 금지 (`NPCActionKeys` 상수 사용).
2. **Blackboard(BB) 단일 진입점**:
   - `NPCActionComponent`에서 BB를 직접 쓰지 말 것.
   - 유일한 BB 쓰기 진입점은 `SmartNPCAIController`의 핸들러뿐이다.
3. **코드 주석 및 인수인계**:
   - 모든 설명과 주석은 **한국어**로 작성. 주석은 문서 번호 참조 없이 자체 완결적으로.
   - 작업 시작 시 `docs/Memo.md`의 `## Todo`를 확인하고, 완료 시 `## Done`에 기록.

---

## 3. Progressive Disclosure Index (도메인별 세부 규칙 라우팅)
작업 대상 도메인에 따라 필요한 하위 규칙만 선택적으로 참조하십시오:

| 작업 도메인 | 참조 규칙 경로 | 주요 내용 |
| :--- | :--- | :--- |
| **UE5 C++ 개발** | [`.agents/rules/ue5_cpp.md`](.agents/rules/ue5_cpp.md) | `EAction` 4곳 수정, EQS 에셋 정책, 널 가드 |
| **Python 인지 서버** | [`.agents/rules/python_backend.md`](.agents/rules/python_backend.md) | Envelope 스키마, LangGraph, SLM 토큰 다이어트 |
| **빌드 & 검증 (SoL-Pi)** | [`.agents/rules/sol_pi.md`](.agents/rules/sol_pi.md) | `tools/sol_pi.py verify` (액션 퓨전: 빌드+테스트) |
| **3D 에셋 & VR 최적화**| [`.agents/rules/mesh_doctor.md`](.agents/rules/mesh_doctor.md) | `tools/mesh_doctor.py` 지오메트리 치료 및 콜리전 감사 |
| **Git 커밋·브랜치·PR** | [`.agents/rules/git.md`](.agents/rules/git.md) | 브랜치 전략, 메시지 형식, `.uasset` 커밋 단위 |
| **세션 진행 및 백로그** | [`docs/Memo.md`](docs/Memo.md) | 세션 간 인수인계 단일 진실 원천(SSOT). `## Todo` / `## Done` / `## Handoff Notes` |
| **사용자 작업 목록** | [`docs/DoList.md`](docs/DoList.md) | 에이전트가 MCP 로 못 하는 것만 (§5) |

---

## 4. Working Style (작업 방식)
- **Ask-Before-Choose**: 새 기능/리팩토링 시작 시 선택지를 묶어 사용자에게 확인. 임의 기본값 진행 금지.
- **Critical Reception**: 요청 수행 전 비판적 검토. 기술적 불가능/비현실적 요청 시 확인 가능한 사실(용량·존재 여부·버전)을 먼저 검증하고 숫자와 근거를 들어 대안 제시 (`[반론] <문제(근거)> → <대안>`).

---

## 5. Due Diligence Guard (검증 경계)
- **C++ 수정** (`Source/**/*.h`·`.cpp`): `python tools/sol_pi.py verify all` (빌드 + pytest + UAT). 에디터 실행 중 제약은 `ue5_cpp.md` §5.
- **Python 만 수정** (`OmniAgent_VR_System/**/*.py`): `python tools/sol_pi.py verify python` (빌드 생략, pytest 만).
- **문서·마크다운·주석·UI 텍스트만 수정**: 빌드·테스트 생략.
- **3D 툴 호출 경계**: 3D 에셋 임포트 또는 지오메트리 결함이 보고되었을 때만 `python tools/mesh_doctor.py`를 호출한다.
- **Python·스크립트 검증은 에이전트가 직접** 실행·확인까지 끝낸다. 사용자에게 미루지 않는다.
- **에디터 작업은 ue5 MCP 로 먼저**: 에셋·BP 생성, 위젯 트리, 프로퍼티, 레벨 배치, 헤드셋 없는 PIE 는 `ue_run_python`/`ue_call_function` 으로 시도. MCP 가 끊겨 있으면 에디터 기동·재연결까지 해본 뒤, 실패한 것만 시도 내역과 함께 `docs/DoList.md` 에 등록. 처음부터 사람 몫인 것: BP 그래프 노드 편집, 헤드셋 PIE 육안 검증(에이전트는 체크리스트 제시), GitHub UI·운영 결정.
