# SPEC — 가독성 리팩토링 계획 (VRPawn·HUD·Voice)

> **2026-09-12 대체됨** — `SPEC_refactor_encapsulation.md` §6 이 실행 계획의 단일 소스. R1→C1, R2→음성 파이프라인 삭제로 소멸, R3→2단계 이후 선택. 이 문서는 근거 기록용으로만 남긴다.

작성 2026-09-11 / 시니어 재판정 2026-09-11. behavior-preserving(출력 불변). 대상: `AVRPawn`·`UPlayerHUDWidget`·`UVoiceInputComponent`.
목적: **가독성**. 기능 추가·변경 없음.

> **재판정 요지**: `.uasset` 바이트코드 + C++ 교차검증 결과, 최대 실이득은 God-class 분해가 아니라 **죽은 API 삭제**였다.
> "통합(dedup)" 전에 "이 코드 필요한가"를 먼저 물었어야 했다. 초안의 F1·F3·F5·F6 은 반려(사유 하단).

원칙:
- 삭제 > 통합 > 추상화. 호출처 0 이면 dedup 이 아니라 delete.
- 한 커밋 = 한 논리 변경. C++ 수정 후 Build.bat 검증. Python 무관.
- rule-of-three: 호출부 3곳 미만이면 헬퍼로 묶지 않는다.
- 출시 중인 코드에 재사용·테스트 이득 없는 리팩토링은 걸지 않는다.

---

## 채택 (실행)

### R1. HUD 헬스/스태미나 게터 6개 삭제  ★최대 실이득
**대상**: `Source/UE5_MCP_VR/UI/BP/PlayerHUDWidget.{h,cpp}`
**대상 함수**: `GetCurrentHealth`·`GetMaxHealth`·`GetHealthPercent`·`GetCurrentStamina`·`GetMaxStamina`·`GetStaminaPercent`.

**근거(검증)**:
| 함수 | WBP 바이트코드 | C++ 외부 | 내부 |
|---|---|---|---|
| `GetCurrentHealth`/`GetMaxHealth` | 0 | 0 | `GetHealthPercent` 만 |
| `GetHealthPercent` | 0 | 0 | 없음 |
| `GetCurrentStamina`/`GetMaxStamina` | 0 | 0 | `GetStaminaPercent` 만 |
| `GetStaminaPercent` | 0 | 0 | 없음 |

- `NativeTick` 은 `IPlayerBase::Execute_GetPlayerAttributes` 를 **직접** 읽어 바/텍스트를 그린다 — 게터 경유 아님.
- 앞서 grep 에 잡힌 `GetHealthPercent` 참조 3건은 전부 `FPlayerResources::GetHealthPercent()`(`CharacterAttributes.h`) — **동명 다른 클래스**. HUD 게터와 무관.
- 6개는 서로만 부르는 폐쇄 클러스터 → 통째 삭제 가능.

**절차**: 헤더 선언 6개 + cpp 정의 6개 삭제. `--- HP ---`/`--- Stamina ---` 섹션 주석 정리.
**검증**: Build.bat exit 0. `pytest tests/` 불변(C++ 무영향).
**위험**: 없음. BlueprintPure 라 이론상 WBP 호출 가능하나 바이트코드 0 으로 확인됨.

### R2. Voice abort 시퀀스 3중복 → `AbortStreaming()`
**대상**: `Source/UE5_MCP_VR/Core/Components/VoiceInputComponent.{h,cpp}`
**현재**: `OnConnectionError`([:76](../Source/UE5_MCP_VR/Core/Components/VoiceInputComponent.cpp))·`OnClosed`([:100](../Source/UE5_MCP_VR/Core/Components/VoiceInputComponent.cpp)) 람다가 `bTalking=false` + StopStream/CloseStream(이미 `StopAudioCaptureStream()` 존재하나 미사용) + `SetComponentTickEnabled(false)` 인라인. OnClosed 는 `bStartSent=false` 추가.
```cpp
// private
/** WS 오류·원격종료 공통 정리 — 발화 중단·캡처 정지·틱 끄기. */
void AbortStreaming();
```
```cpp
void UVoiceInputComponent::AbortStreaming()
{
    bTalking = false;
    StopAudioCaptureStream();
    SetComponentTickEnabled(false);
    bStartSent = false;
}
```
- 두 람다 → `StrongThis->AbortStreaming();`.
**검증**: Build.bat. `bStartSent=false` 공통화 무해(OnConnectionError 는 start 전이라 이미 false).
**위험**: 없음. 호출부 2곳이지만 셋째 개념 후보(EndPlay 정리 경로)와 동형이라 rule-of-three 정신 충족 + 정리 로직 단일화가 안전성 이득.

---

## 선택 (저이득, 급하지 않음)

### R3. VRPawn 손 kinematics 짝 → `FVRHandKinematics Hands[2]`
**대상**: `Source/UE5_MCP_VR/Core/BP/VRPawn.{h,cpp}`
**안전 분리분만**(비컴포넌트, BP 무참조):
```cpp
struct FVRHandKinematics { FVector PrevLoc = FVector::ZeroVector; FVector Vel = FVector::ZeroVector; };
FVRHandKinematics Hands[2];   // [0]=Left, [1]=Right
```
- `PrevHandLocLeft/Right`·`HandVelLeft/Right` → `Hands[0/1]` ([Tick :338-358](../Source/UE5_MCP_VR/Core/BP/VRPawn.cpp)).
- 접근 헬퍼 `UMotionControllerComponent* HandController(bool bLeft) const` 로 `bLeft ? …Left : …Right` 삼항 반복([1342](../Source/UE5_MCP_VR/Core/BP/VRPawn.cpp)·[1384](../Source/UE5_MCP_VR/Core/BP/VRPawn.cpp)) 단일화.
**❌ 하지 말 것**: `MotionControllerLeft/Right`·`…Aim`·`MeleeSphereLeft/Right` = `CreateDefaultSubobject` 컴포넌트 → BP_VRPawn 이 이름 참조·부착. 배열화 시 BP 부착 전부 깨짐. **현행 유지.** `Left/RightHandGripOffset` = `UPROPERTY(EditAnywhere)` → BP Details 튜닝값 직렬화됨, 배열화 시 유실. **현행 유지.**
**위험**: kinematics 분리만 저위험. 그 이상 손대면 BP 파손.

---

## 반려 (초안 철회 — 사유)

| 초안 | 반려 사유 |
|---|---|
| **F1** HUD 속성조회 헬퍼 | 대상 게터 6개가 **죽은 코드**(R1) → 삭제하면 남는 조회는 `NativeTick` 1곳. 헬퍼 불필요. |
| **F3** `UpdateResourceBar(…, bIntTextOnly)` | 불리언 플래그 인자 = flag-argument anti-pattern(함수가 두 일). 호출부 2곳 = rule-of-three 미달. 두 블록이 지금도 읽힌다 — 모드 스위치가 가독성을 **낮춘다**. |
| **F5** 대쉬 복원 구조체 | float 3개 저장/복원에 Capture/Restore 구조체는 ceremony. YAGNI. |
| **F6** God-class 분해(a/b/c/d) | **전면 반려.** 추출 payoff=재사용+테스트 용이성 → 둘 다 없음(폰 1개, 유닛테스트 0). 비용=BP 서브오브젝트 결합(컴포넌트 폰 밖 이동 시 BP 참조 silent-break, K2Node·WidgetTree 미노출로 매번 수작업 재배선). VR 폰은 본질적으로 큼 — 분해는 복잡도를 없애지 않고 BP 경계 너머로 옮겨 **추론을 더 어렵게** 함. 가독성은 이미 Tick 헬퍼 그룹핑 + `AllowPrivateAccess` 섹션으로 확보. 출시 중 코드에 이득 없는 churn. |

---

## 손대지 말 것 (죽은 게 아니라 미완성)

- `UPlayerHUDWidget::UnequipSlot`·`GetEquippedSlotItem` — WBP 0·C++ 0 이나 **미구축 장비 슬롯 UI 훅**. 삭제 금지, 기능 완성 대기.
- 디버그 Exec(`TuneGrab`·`LogIKMetrics`·`DumpInventoryHUD`·`Cheat_Unequip`·`ToggleInventory`) — 의도된 개발 도구. 유지.

---

## 실행 순서

| ID | 내용 | 위험 | 커밋 |
|---|---|---|---|
| R1 | 죽은 HUD 게터 6개 삭제 | 없음 | `refactor: 미사용 HUD 게터 6개 삭제 — PlayerHUDWidget` |
| R2 | Voice AbortStreaming | 없음 | `refactor: Voice abort 시퀀스 통합 — AbortStreaming` |
| R3 | 손 kinematics 배열(선택) | 낮음 | `refactor: 손 kinematics Hands[2] 그룹핑 — VRPawn` |

1. **R1 → R2** 바로 (빌드 검증, 커밋 2개).
2. **R3** 는 손 관련 다른 작업 낄 때 묶어서(단독 급하지 않음).
3. **F6 은 열지 않는다** — 향후 폰이 실제로 재사용되거나 컴포넌트 유닛테스트를 도입할 때 재검토.
