---
name: add-eaction
description: EAction 신규 추가 시 4곳 체크리스트 강제 실행 + 완료 모델 분기 확인
---

## Add EAction

새 `EAction` enum 값 추가 시 StateTree가 인식하려면 4곳을 **동시에** 수정해야 합니다.
한 곳이라도 빠지면 런타임에서 조용히 깨집니다.

### 사용법

```
/add-eaction <ActionName>
```

예: `/add-eaction Crouch`

### 체크리스트 (순서대로 실행)

1. **`NPCActionTypes.h`** — `EAction` enum에 `<ActionName>` 추가
2. **`NPCActionComponent.cpp::GetGameplayTagForAction`** — `case EAction::<ActionName>:` 태그 케이스 추가
3. **`NPCActionComponent.cpp::ExecuteInteraction`** — `case EAction::<ActionName>:` switch 케이스 추가, `Execute<ActionName>()` 호출
4. **`NPCActionComponent.cpp`** — `Execute<ActionName>()` 구현 함수 작성

### 완료 모델 분기 확인 (4번 완료 후 필수)

구현 함수 작성 후 아래 중 하나를 반드시 선택하고 명시:

| 유형 | 조건 | 처리 |
|------|------|------|
| **즉시형** | 실행 후 즉시 완료 | switch 말미에서 `OnActionCompleted()` 자동 호출 — 추가 작업 없음 |
| **이동형** | `BaseMove` 경유 | `bActionAwaitingAsync = true`, `OnMoveActionCompleted` 콜백이 `OnActionCompleted` 호출 |
| **몽타주형** | `BasePlayActionMedia` 경유 | `bActionAwaitingAsync = true`, `OnMontageActionEnded` 콜백이 `OnActionCompleted` 호출 |
| **장시간 기타** | 위 둘 다 아님 | 직접 콜백/타이머로 `OnActionCompleted()` 호출 필수 — 미호출 시 `MaxActionDuration` 워치독이 강제 완료 |

### 완료 후

모든 4곳 수정 + 완료 모델 선택이 끝나면 수정 파일 목록과 선택한 완료 모델을 보고합니다.
