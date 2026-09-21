# UE5-MCP-VR Orchestrator — Agent Guidelines (AGENTS.md)

이 문서는 모든 자율형 코딩 에이전트(Antigravity, Claude Code, Cursor 등)가 공통으로 준수해야 하는 최상위 프로젝트 인덱스(Meta-README)입니다.
불필요한 토큰 낭비를 방지하기 위해 **점진적 정보 노출(Progressive Disclosure)** 원칙을 따르며, 세부 도메인 작업 시 하위 규칙을 선택적으로 열람하십시오.

---

## 1. System Architecture (시스템 개요)
- **Client (UE5)**: 언리얼 엔진 5.5 C++ & Blueprint 기반 VR 게임. 물리(Chaos), IK, StateTree 중심의 결정론적 런타임 실행.
- **Server (Python)**: FastAPI & WebSocket 기반 비동기 인지 엔진. 로컬 SLM(Ollama) 및 정책 기반 의사결정.
- **Protocol**: UE5와 Python은 `MessageEnvelope` 규격의 JSON으로 실시간 양방향 통신.

---

## 2. Inviolable Core Principles (절대 위반 불가 3대 원칙)
1. **JSON 통신 규격**:
   - 모든 통신은 `MessageEnvelope`로 래핑.
   - Parameters 내부는 `snake_case`, 최상위는 `PascalCase`. 리터럴 문자열 금지 (`NPCActionKeys` 상수 사용).
2. **Blackboard(BB) 단일 진입점**:
   - `NPCActionComponent`에서 BB를 직접 쓰지 말 것.
   - 유일한 BB 쓰기 진입점은 `SmartNPCAIController`의 핸들러뿐이다.
3. **코드 주석 및 인수인계**:
   - 모든 설명과 주석은 **한국어**로 작성.
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
| **세션 진행 및 백로그** | [`docs/Memo.md`](docs/Memo.md) | 세션 간 인수인계 단일 진실 원천(SSOT) |

---

## 4. Due Diligence Guard (과도한 실사 오버헤드 방지)
- **빌드 실행 경계**: C++ 로직(`.h`/`.cpp`)을 수정했을 때만 `python tools/sol_pi.py build`를 실행한다. 단순 문서, 마크다운, UI 텍스트, Python 파일 변경 시에는 빌드를 실행하지 않는다.
- **3D 툴 호출 경계**: 3D 에셋 임포트 또는 지오메트리 결함이 보고되었을 때만 `python tools/mesh_doctor.py`를 호출한다.

