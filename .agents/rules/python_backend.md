---
trigger: glob
globs: OmniAgent_VR_System/**/*.py
---

# Python 인지 백엔드 개발 규칙 (python_backend.md)

이 규칙은 `app/` 디렉토리 내의 Python 3.10+ 인지 엔진 및 WebSocket 서버 코드를 작성하거나 수정할 때 적용됩니다.

---

## 1. 새 Envelope 메시지 타입 추가 동시 수정 규칙
새로운 메시지 타입을 추가할 때는 아래 **3곳을 반드시 동시에 수정**해야 무음 무시(Silent Drop)를 방지할 수 있습니다:
1. `Source/UE5_MCP_VR/Network/EnvelopeBuilder.h/.cpp` — `EEnvelopeType` 값 + `EnvelopeTypeToString` 분기 + `Build*(const TSharedRef<FJsonObject>&)` 헬퍼 추가
2. `app/schemas/envelope.py` — `EEnvelopeType` enum에 타입 추가
3. `app/main.py::_process_llm_message` — 타입별 분기에 `_handle_*` 핸들러 연결 (`tests/test_contract_sync.py` 가 세 곳의 정합을 검사)

---

## 2. JSON 키 규칙 및 데이터 검증
- **Parameters 내부**: 반드시 `snake_case` 사용.
- **최상위 Envelope**: 반드시 `PascalCase` 사용.
- **키 상수화**: C++ 쪽은 `NPCActionKeys` 상수를 사용하므로, Python 출력 스키마(`app/schemas/actions.py`)에서 임의로 키 철자를 변경하지 마십시오. C++ `MCPJsonUtils`에 폴백 키를 임의로 추가하지 말고 Python 스키마를 단일 소스로 맞추십시오.

---

## 3. 코딩 표준 및 퍼포먼스
- **타입 힌팅 (Type Hinting)**: 모든 함수 인자 및 반환값에 엄격한 타입 힌트 적용.
- **로깅 표준**: 원시 `print()` 사용을 금지하고, 반드시 `logging.getLogger(__name__)` 인스턴스를 통해 로그 레벨(debug/info/warning/error)을 지정.
- **로컬 SLM(gemma4 e4b 파인튜닝, `llm_factory.STAGE1_MODEL`) 토큰 최적화**:
  - 불필요한 프롬프트 주입을 피하고 토큰 다이어트 준수.
  - 대화 기록 주입량은 최근 5턴 이내로 제한.

