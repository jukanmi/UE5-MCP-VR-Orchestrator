# SPEC_player_systems.md

**상태**: 2026-08-22 인터뷰 완료 → **2026-08-24 구현 완료(C++), PIE 미검증**  

> **2026-08-24 구현 시 뒤집힌 전제 3건** — 아래 본문은 원안이며, 실제 구현은 이 항목을 따른다.
> 1. **`APickupItem` 신규 폐기 → `ADroppedItemBase` 재사용**(§3). 기존 클래스가 `ItemManager` 등록/해제·InteractionSphere·`ConsumeItem()` 을 이미 갖고 있고 NPC 아이템 탐지가 그 등록 풀을 본다. 신규 클래스는 미등록이라 NPC 가 인지 못 함. 추가한 것은 `Amount` 필드 1개 + `FDroppedItemData::ItemTemplateID` 를 `EditAnywhere` 로 승격(그 전엔 에디터 입력 불가로 배치품이 전부 `DefaultEntity_Unknown` 이 됐음).
> 2. **HandObject 는 플레이어 인벤토리에 넣지 않는다**(§4). 원안(NPC 보유 유지 + 플레이어 추가)은 아이템 복제. HandObject = 제시 연출 전용, 실제 이전은 GiveItem 담당으로 확정.
> 3. **Sprint 해제는 `OnMove` 가 아니라 `IA_Move` Completed/Canceled**(§1.2). `IA_Move` 는 `Triggered` 만 바인딩돼 있고 `OnMove` 가 `Input.IsNearlyZero()` 에서 조기 return 하므로, 원안의 해제 코드는 실행될 수 없었다. `OnMoveReleased()` 신설.
>
> 부수 정정: §4.3 의 `NPCInventoryComponent::GetItemDataByID` 신규는 불필요 — `UItemManager::GetItemDataByID` 가 이미 있고 `ExecutePickUp` 이 그 idiom 을 쓰고 있다.

**브랜치**: `feature/player-systems` (Develop 분기)  
**담당 범위**: Sprint · 인벤토리 HUD 열기 · 월드 아이템 픽업 · NPC 아이템 전달 연동  
**제외**: 구르기(별도 결정), VRPlayerCharacter 제거(별도 세션)

---

## §1 달리기 (Sprint)

### §1.1 현황

`AVRPawn::ApplyMovementSpeed()` (`VRPawn.cpp:833`) — Standing 일 때 `WalkSpeed(200)` 고정.  
`FMovementAttributes.SprintSpeed = 600.0f` (`CharacterAttributes.h:186`) 이미 존재하나 VRPawn에서 미사용.

### §1.2 트리거 결정

스틱 magnitude > 0.9 시 Sprint. 별도 입력 액션 추가 없음.

- `OnMove()` (`VRPawn.cpp:453`) 에서 `Input.Size() > SprintThreshold(0.9f)` 판정
- `bIsSprinting` bool 플래그 갱신(멤버 변수 신설)
- 스틱을 놓으면(`Input.IsNearlyZero()`) `bIsSprinting = false` + `ApplyMovementSpeed()` 즉시 원복

### §1.3 수정 범위

| 파일 | 변경 |
|------|------|
| `VRPawn.h` | `bool bIsSprinting = false;` / `float SprintThreshold = 0.9f` (EditAnywhere) 추가 |
| `VRPawn.cpp::OnMove` | `Input.Size() > SprintThreshold` → `bIsSprinting = true; ApplyMovementSpeed()` |
| `VRPawn.cpp::ApplyMovementSpeed` | Standing 분기: `bIsSprinting ? SprintSpeed : WalkSpeed` |

### §1.4 제약

- Crouching/Prone 중 Sprint 금지 — 자세 전환 시 `TransitionTo()` 가 `ApplyMovementSpeed()` 호출하므로 자동으로 억제됨
- NPC `EmovType::Sprint` 과 무관 — VRPawn 전용

---

## §2 인벤토리 HUD 열기

### §2.1 현황

`PlayerHUDWidget` (`PlayerHUDWidget.h:24`):
- `RequestInventoryRefresh()` — `OnInventoryUpdated` BIE 트리거
- `OnInventoryUpdated` — WBP에서 슬롯 재구성 구현(에디터 작업)
- `GetInventorySlots()` — `TArray<FInventorySlot>` 반환
- **열기/닫기 토글 없음** — 이번에 추가

### §2.2 구현 방식

C++ 측: `PlayerHUDWidget` 에 `SetInventoryPanelVisible(bool)` UFUNCTION 추가  
WBP 측: 인벤토리 패널(`SizeBox/VerticalBox`) 초기 Hidden, `SetInventoryPanelVisible` 에서 토글(에디터 작업)

VR 입력: **왼손 Y버튼** → `IA_InventoryToggle` (InputAction 신규) → `OnInventoryToggle()` 핸들러  
→ `bInventoryOpen = !bInventoryOpen` → `HUDWidget->SetInventoryPanelVisible(bInventoryOpen)`

### §2.3 수정 범위

| 파일 | 변경 |
|------|------|
| `PlayerHUDWidget.h/.cpp` | `SetInventoryPanelVisible(bool)` + `bInventoryVisible` + `ToggleInventoryVisibility()` |
| `VRPawn.h` | `IA_InventoryToggle` UInputAction + `bInventoryOpen` + `OnInventoryToggle()` 핸들러 |
| `VRPawn.cpp::SetupPlayerInputComponent` | `IA_InventoryToggle` 바인딩 |

### §2.4 에디터 작업 (DoList)

- IMC_VR 에 왼손 Y버튼 → `IA_InventoryToggle` 매핑 추가
- WBP_PlayerHUD: 인벤토리 패널(슬롯 그리드) 추가, `SetInventoryPanelVisible` 구현

---

## §3 월드 아이템 픽업

### §3.1 현황

`APickupItem` 클래스 없음 — 신규 생성 필요.  
`UInventoryComponent::AddItem()` 이미 완성 (`InventoryComponent.h:96`).  
`VRPawn::OnInteract` — 착석 판정 후 NPC 감지로 넘어감. 픽업 판정 삽입 지점.

### §3.2 APickupItem 설계

```
Source/UE5_MCP_VR/Inventory/PickupItem.h/.cpp
```

```cpp
UCLASS()
class APickupItem : public AActor
{
    // 인벤토리에 등록할 아이템 데이터
    UPROPERTY(EditAnywhere, Category="Pickup")
    FItemData ItemData;

    UPROPERTY(EditAnywhere, Category="Pickup")
    int32 Amount = 1;

    // 픽업 반경 — VRPawn Interact 판정용
    UPROPERTY(EditAnywhere, Category="Pickup", meta=(ClampMin="30", ClampMax="300"))
    float PickupRange = 150.f;

    // 플레이어가 줍는다
    bool TryPickup(UInventoryComponent* PlayerInventory);
};
```

`TryPickup` 성공 시: `AddItem` → `OnInventoryChanged` → HUD 자동 갱신 → `Destroy()`.

### §3.3 VRPawn Interact 수정

`TrySitOnNearbyFurniture()` 검사 전(착석 우선은 유지) → **픽업 검사를 착석 검사 다음, NPC 감지 전**에 삽입.

```
Interact 흐름:
  착석 중? → 기상
  근접 픽업 아이템(150cm)? → TryPickup → 반환  ← 신규
  착석 가능 가구? → 착석
  NPC 감지
```

`TryPickupNearby()` private 헬퍼 신설 → `SphereOverlapActors(APickupItem)` → 최근접 → `TryPickup`.

### §3.4 수정 범위

| 파일 | 변경 |
|------|------|
| `Inventory/PickupItem.h/.cpp` | 신규 |
| `VRPawn.cpp::OnInteract` | `TryPickupNearby()` 검사 삽입 |
| `VRPawn.h` | `TryPickupNearby()` 헬퍼, `PickupInteractRange` (EditAnywhere, 150.f) |

---

## §4 NPC 아이템 전달 연동 (HandObject / GiveItem)

### §4.1 현황 문제

`ExecuteHandObject()` (`NPCActionComponent.cpp:2134`):
- NPC 인벤토리에서 `EquipItem` 만 함(손에 드는 연출)
- 플레이어 인벤토리 미추가

`ExecuteGiveItem()` (`NPCActionComponent.cpp:2120`):
- NPC 인벤토리에서 `RemoveItem` + Give 몽타주
- 플레이어 인벤토리 미추가 — **아이템이 증발함**

### §4.2 타겟 플레이어 찾기

`TargetActor` 파라미터 이미 있음. `IPlayerBase` 인터페이스 여부로 플레이어 판정:

```cpp
if (UInventoryComponent* PlayerInv = TargetActor
        ? TargetActor->FindComponentByClass<UInventoryComponent>()
        : nullptr)
{
    PlayerInv->AddItem(ItemData, Amount);
}
```

- `TargetActor` 없을 때: `NPCManager::GetPlayerActor()` 폴백(현재 월드 내 VRPawn 탐색).

### §4.3 ItemData 획득 문제

`ExecuteHandObject(ItemID)` 는 ID만 있고 `FItemData` 가 없음.  
→ `NPCInventoryComponent` 또는 `ItemDataAsset`(DataTable 기반) 에서 ID로 조회.  
`NPCInventoryComponent` 가 소유 NPC 아이템 원천 → `GetItemDataByID(ItemID)` 추가 또는  
`InventoryComponent::GetSlotByItemID()` 활용 (이미 `GetSlotIndexByItemID` 존재 `:164`).

### §4.4 수정 범위

| 파일 | 변경 |
|------|------|
| `NPCActionComponent.cpp::ExecuteGiveItem` | 성공 분기 + 플레이어 인벤토리 `AddItem` |
| `NPCActionComponent.cpp::ExecuteHandObject` | 동일(HandObject는 전달 후 NPC는 보유 유지) |
| `NPCInventoryComponent.h/.cpp` | `GetItemDataByID(FString)` 헬퍼 (없으면 신규) |

### §4.5 Python 쪽 변경 없음

`HandObject` / `GiveItem` Envelope 타입은 기존 그대로.  
아이템 ID는 LLM이 이미 내보내고 있음 — 수신·실행 경로만 보완.

---

## §5 구현 진입점

| 작업 | 파일 : 줄 |
|------|-----------|
| Sprint 플래그 추가 | `VRPawn.h` (멤버 변수 섹션) |
| Sprint 판정 | `VRPawn.cpp:453` `OnMove` |
| Sprint 속도 적용 | `VRPawn.cpp:838` `ApplyMovementSpeed` Standing 분기 |
| 인벤토리 토글 바인딩 | `VRPawn.cpp::SetupPlayerInputComponent` |
| 픽업 흐름 삽입 | `VRPawn.cpp::OnInteract` (착석 로직 바로 아래 ~:430) |
| GiveItem 연동 | `NPCActionComponent.cpp:2120` `ExecuteGiveItem` |
| HandObject 연동 | `NPCActionComponent.cpp:2134` `ExecuteHandObject` |
| HUD 토글 메서드 | `PlayerHUDWidget.h:28` public 섹션 |

---

## §6 PIE 검증 체크리스트

1. 스틱 최대값 밀기 → Sprint(600 UU/s) → 스틱 놓으면 Walk(200) 즉시 원복
2. 크라우치 중 스틱 최대 밀기 → Sprint 발동 안 됨(CrouchSpeed 유지)
3. 왼손 Y버튼 → 인벤토리 패널 열림 → 재입력 → 닫힘
4. 월드에 `BP_PickupItem` 배치 → A버튼(150cm 내) → 슬롯에 추가 + HUD 갱신
5. NPC `GiveItem` 응답 → 플레이어 인벤토리에 실제로 추가됨
6. NPC `HandObject` 응답 → 플레이어 인벤토리에 추가 + NPC 보유 유지

---

## §7 미결 사항

- [x] **구르기 → 대쉬로 확정** (2026-09-04). 구르기는 시야 회전을 만들어 VR 멀미의 주범이
      되므로 회전 없는 등속 대쉬로 대체했다. `AVRPawn::OnDash`/`UpdateDash`/`StopDash` —
      NPC 의 `StartDodgeMove` 패턴(마찰·제동 0 → Launch → 잔류 속도 제거)을 재사용하되
      액터 회전 스냅은 제거. 오른손 B버튼, 왼손 스틱 방향, 무적 프레임 없음.
      `IA_Dash` 에셋·IMC 매핑은 DoList 1-6.
- [x] **IMC_VR Y버튼 매핑** — 2026-08-24 완료(`OculusTouch_Left_Y_Click`)
- [x] **WBP_PlayerHUD 인벤토리 패널** — 2026-08-30 완료(`SlotGrid` 5열 + `InventoryPanel`)
- [x] **VRPlayerCharacter 제거** — 클래스·에셋 모두 없음. `PlayerInteractionUtils.h` 주석에만
      이름이 남아 있으나 코드 참조는 0.
- [x] **NPCInventoryComponent::GetItemDataByID** — 불필요. `UItemManager::GetItemDataByID` 가
      이미 있고 `NPCActionComponent` 가 이를 사용 중이라 신규 헬퍼를 만들지 않았다.
