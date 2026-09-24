---
trigger: always_on
---

# SoL-Pi (Speed-of-Light Pipeline) Harness Rule

1. **액션 퓨전 (Action Fusion — `verify`)**:
   - C++ 로직 또는 핵심 시스템 수정 후, 빌드와 회귀 테스트를 단일 도구 호출로 묶어 검증한다:
     ```powershell
     python tools/sol_pi.py verify [all | python | engine]
     ```
   - 빌드 실패 시 즉시 중단(Fast-fail)하며, 성공 시 테스트 결과를 포함한 단일 컴팩트 영수증(Compact Receipt)을 확인한다.

2. **실행 및 생략 경계 (Execution Guard)**:
   - **언제 실행하는가**: `Source/UE5_MCP_VR/` 내 C++ 로직(`.h`/`.cpp`)을 수정했으면 `verify all`, Python `app/` 코드만 수정했으면 `verify python`(C++ 빌드 생략, pytest 만)을 실행한다.
   - **언제 생략하는가**: 단순 주석/오타 수정, 마크다운 문서, UI 텍스트 변경 시에는 빌드/테스트를 실행하지 않는다.
   - **루프 방지 가드**: 동일한 컴파일/테스트 에러가 2회 연속 발생하면 무한 자가 수정 루프를 중단하고 즉시 사용자에게 상황을 보고한다.

3. **로그 슬라이싱 및 관측 압축 (ObservationPack)**:
   - 원시 로그 전체(수천 줄)를 컨텍스트에 덤프하지 않는다.
   - 원시 로그는 `Saved/Logs/sol_pi_raw.log`에 자동 아카이빙되며, 컨텍스트에는 슬라이싱된 핵심 에러 및 영수증만 주입한다.
   - 런타임 로그 점검: `python tools/sol_pi.py log`
   - 빌드 단독 검증: `python tools/sol_pi.py build`
   - 테스트 단독 검증: `python tools/sol_pi.py test [all | python | engine]`

4. **위상 인식 파싱 & Watchdog 타임아웃**:
   - **UHT 위상 우선 격리**: UHT 리플렉션 에러 발생 시 후속 MSVC 연쇄 에러를 마스킹하고 근원지 UHT 에러만 우선 노출.
   - **샌드위치 슬라이싱**: 대량 에러 발생 시 최초 5개(원인)와 마지막 1개(결과)만 보존하고 중간 에러 절단.
   - **Watchdog 감시**: UAT 엔진 테스트 120초, C++ 빌드 300초 타임아웃 초과 시 프로세스를 강제 종료(Kill)하여 데드락/무한 루프 방지.
