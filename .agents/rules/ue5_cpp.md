---
trigger: glob
globs: Source/**/*.{h,cpp}
---

# UE5 C++ 개발 및 아키텍처 규칙 (ue5_cpp.md)

이 규칙은 `Source/UE5_MCP_VR/` 내 C++ 헤더(`.h`) 및 소스(`.cpp`)를 작성하거나 수정할 때 적용되는 단일 진실 공급원(SSOT)입니다.

---

## 1. 새 EAction 추가 시 4곳 필수 수정 체크리스트
새로운 액션을 추가할 때는 반드시 아래 **4곳을 동시에 수정**해야 StateTree(ST) 및 액션 컴포넌트가 인식합니다:
1. `Source/UE5_MCP_VR/NPC/Struct/NPCActionTypes.h` — `EAction` enum 추가
2. `Source/UE5_MCP_VR/NPC/Action/NPCActionComponent.cpp::GetGameplayTagForAction` — 게임플레이 태그 매핑 케이스 추가
3. `Source/UE5_MCP_VR/NPC/Action/NPCActionComponent.cpp::ExecuteInteraction` — switch 분기 케이스 추가
4. `Source/UE5_MCP_VR/NPC/Action/NPCActionComponent.cpp` — `Execute*()` 실제 실행 함수 구현

> **주의**: 비동기 액션은 `BaseMove` 또는 `BasePlayActionMedia`를 경유하거나 `OnActionCompleted`를 직접 호출해야 합니다. 미호출 시 `MaxActionDuration` 워치독에 의해 강제 종료됩니다.

---

## 2. Blackboard(BB) 쓰기 책임 원칙
- `NPCActionComponent`에서 Blackboard를 직접 쓰지 마십시오.
- **유일한 BB 쓰기 진입점**: `SmartNPCAIController` 내부의 핸들러만 담당합니다:
  - `HandleAllActionsStopped`
  - `OnTargetPerceptionUpdated`
  - `ExitCombat` (전투 해제 시 `Key_TargetActor` 클리어)

---

## 3. EQS (Environment Query System) 에셋 및 세대(Generation) 정책
- **에셋 2개 제한**: `/Game/Core/AI/EQS_Query/EQS_TacticalPositions` (전술 위치용) 및 `EQS_Move` (LLM 이동 지시용) 2개만 사용. 신규 EQS 에셋 추가 금지 (`UpdateEQSParams()` Named Parameter 사용).
- **Generation 규칙**: `TacticalQueryGeneration` 증가는 `StartTacticalQuery` 한 곳에서만 수행. `ETacticalQueryState`에 터미널 상태 추가 금지 (실패/완료 모두 즉시 Idle 복귀).

---

## 4. 공유 헬퍼 및 NPC 상태 소유권 (중복 구현 금지)
- **Perception 위협도**: `StateComp->ComputePerceptionDanger(BaseDanger, TargetID)` 사용 (인라인 재구현 금지).
- **상태 태그**: `GameplayTagUtils::AddState/RemoveState` 사용 (`GameplayTags.AddTag/RemoveTag` 직접 호출 금지).
- **액션 상태 리셋**: `ClearActiveActionState()` 일괄 정리.
- **상태 소유권**:
  - 현재 액션: `NPCActionComponent::CurrentAction` (`GameplayTags`는 파생 미러).
  - BehaviorMode: `NPCStateComponent::CurrentBehaviorMode` — 값은 `Combat`/`Common` 2개뿐(대분류 없음). `ExecuteActionBatch`·`TryReflexReact`·`ExitCombat`에서만 갱신.
  - 대상 관계(Hostile/Neutral/Friendly): `NPCStateComponent::GetRelation` (임계 비교 중복 금지).

---

## 5. 코딩 표준 & 빌드 가드
- **타입 안정성**: 포인터 널 체크(`IsValid()`, `nullptr`) 및 안전한 형변환(`Cast<T>`) 강제.
- **VR 퍼포먼스**: 매 프레임 `Tick` 사용을 지양하고 이벤트 주도(Event-Driven) 방식 지향.
- **빌드 검증**: 로직 수정 후 `python tools/sol_pi.py verify all`(빌드+pytest+UAT)로 컴파일 오류 0건·테스트 통과를 자체 검증. 에디터 실행 중이면 Build.bat 이 Live Coding 과 충돌하므로 Live Coding(`Ctrl+Alt+F11`, MCP 로는 `LiveCoding.Compile` 콘솔 명령)으로 컴파일.
  - **단, 클래스·UPROPERTY·UFUNCTION 추가/삭제(레이아웃 변경)는 Live Coding 불가** — 에디터를 닫고 빌드해야 한다. 함수 본문만 바뀐 경우에만 Live Coding.


---

## 6. UMG / Blueprint 경계 규칙
- **UI는 게임 상태를 소유하지 않는다**: 위젯은 읽기만. 상태 변경은 이벤트/델리게이트로 게임 시스템에 위임 (위젯 안에서 체력·인벤토리 직접 수정 금지).
- **갱신은 이벤트 주도**: `NativeTick` 폴링 대신 델리게이트 바인딩. 바인딩은 `NativeConstruct`, 해제는 `NativeDestruct`.
- **숨김은 `Collapsed`**: `ESlateVisibility::Hidden`은 레이아웃 자리를 차지. 자리까지 빼려면 `Collapsed`.
- **리스트는 `UListView` + UObject 엔트리**: 구조체 배열로 위젯 수동 생성·파괴 금지. 자주 뜨는 위젯(데미지 숫자·알림)은 풀링.
- **표시 문자열은 `FText`**: `FString`을 위젯 텍스트로 직접 넘기지 않는다.
- **C++ = 프레임워크, BP = 콘텐츠**: 베이스 클래스·핵심 로직·틱 많은 코드는 C++. 튜닝 값·레이아웃·단순 이벤트 응답(사운드·파티클)은 BP.
- **BP 그래프 상한 ~20노드**: 넘으면 함수 분리 또는 C++ 이관. Tick 안에서 Cast/ForEach 금지 — `BeginPlay`에서 캐시.
- **캐스팅 대신 인터페이스**: BP 간 통신은 `BPI_*` 인터페이스 또는 이벤트 디스패처. 구체 클래스 Cast 체인 금지.
- **UI 프로파일**: `stat slate`, Widget Reflector. VR 목표 UI 예산 < 2ms.
