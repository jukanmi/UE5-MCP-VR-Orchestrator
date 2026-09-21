# SPEC_item_pipeline.md — 아이템 사용·드랍·장착 비주얼 파이프라인

**상태**: 2026-08-31 **M1~M4 구현·빌드·데이터 주입 완료** (커밋 `8cfe5c5`), PIE 검증 대기(§7.3 → DoList)
**브랜치**: `feature/player-systems` (원안 `feature/item-pipeline` 대신 — 선행 플레이어 시스템 작업과 같은 브랜치에서 이어짐)
**담당 범위**: `FItemData` 스키마 확장 · `UseItem`(스탯 효과) · `DropItem`(월드 스폰) · `EquipItem`(손 소켓 메시 부착)
**제외**: Craft 레시피(데이터 설계 선행) · 상점/경제 · 버프/디버프 지속효과
**선행**: `SPEC_item_registry.md`(72행 주입 완료, 2026-08-29)

---

## §1 배경 — 현행 실측 (2026-08-29)

### 1.1 없는 것

| 대상 | 상태 |
|---|---|
| `UInventoryComponent::UseItem` | **함수 자체가 없음** |
| `UInventoryComponent::DropItem` | **함수 자체가 없음** |
| `FItemData` 소비 효과 필드 | **없음** — 회복량을 담을 자리가 아예 없다 |
| `FItemData::WorldMeshClass` | 필드는 있으나 **72행 전부 공란** |

### 1.2 죽어 있는 것

```cpp
// NPCActionComponent.cpp::ExecuteDrop
// TODO: TargetTemplateID → BP 클래스 매핑 후 SpawnActor 구현
AActor* SpawnedItem = nullptr;          // ← 항상 nullptr
if (IsValid(SpawnedItem)) { /* 등록 */ }
else { UE_LOG(Warning, "드랍 스폰 미구현(TODO)"); }
```

`ExecuteDrop` 은 **인벤토리에서 차감만 하고 월드엔 아무것도 안 남긴다**. `GiveItem` 증발 버그(2026-08-24 `60411d3` 수정)와 같은 계열의 증발이 Drop 에 아직 남아 있는 셈.

`ExecuteUseItem` 도 `RemoveItem` + 몽타주뿐 — **먹어도 아무 효과가 없다**.

`EquipItem`/`UnequipItem` 은 데이터 슬롯만 갱신하고 **메시를 붙이지 않는다**. 손이 비어 있다.

### 1.3 왜 지금인가

`SPEC_item_registry` 로 72종을 채웠지만, 그 아이템들이 **할 수 있는 게 없다**. 빵을 먹어도 배가 안 부르고, 검을 장착해도 손에 안 들리고, 버려도 사라지기만 한다. 데이터는 있는데 동사가 없는 상태.

---

## §2 결정사항 (2026-08-29 인터뷰)

### 2.1 소비 효과 정의 = **`FItemData` 회복 필드 3개**

`HealthRestore` · `ManaRestore` · `StaminaRestore` (float). 채택 이유: 현재 소비템 14종이 전부 "HP/마나/스태미나 회복" 범주라 enum 분기가 불필요하고, **복합 효과**(빵 = HP + 스태미나)를 한 행으로 표현할 수 있다.

> 버프·디버프·해독 같은 **지속/상태 효과는 이 SPEC 범위 밖**. 필요해지면 그때 `DT_ItemEffects` 조인 테이블로 확장한다(필드 3개는 그대로 두고 병행 가능).

### 2.2 메시 조달 = **공용 BP + 메시 참조 필드**

`FItemData` 에 `WorldMesh`(`TSoftObjectPtr<UStaticMesh>`) 를 추가하고, 드랍은 기존 `BP_DropItem`(부모 `ADroppedItemBase`) **하나만** 스폰한 뒤 메시를 갈아끼운다. 장착 부착도 같은 메시를 재사용.

- 채택 이유: BP 72개를 만드는 대신 에셋 1개로 전 아이템 커버. 아이템별 고유 물리·상호작용이 필요해지면 그때 `WorldMeshClass`(기존 필드, 유지)로 개별 BP 를 지정하는 이중 경로가 이미 있다.
- **`WorldMeshClass` 는 지우지 않는다** — `WorldMesh`(간편 경로)가 비었을 때만 `WorldMeshClass`(고급 경로)를 보는 폴백 순서로 둔다.

### 2.3 ⚠ 미결 — 컴포넌트가 소유자 스탯을 쓸 방법이 없다

**이 SPEC 최대의 설계 문제다. 구현 착수 전 확정 필요.**

`UseItem` 은 `UInventoryComponent`(컴포넌트)에 들어가는데, 소유자의 HP 를 **쓸 수 있는 합법 경로가 현재 없다**:

```cpp
// Entity.h — 값 반환이라 쓰기 불가
UFUNCTION(BlueprintNativeEvent, Category="Character|Attributes")
FCharacterAttributesBase GetAttributes() const;   // ← const, by value
```

스탯 쓰기는 지금 각 구체 클래스가 **멤버 직접 접근**으로만 한다:

```cpp
// VRPawn.cpp::TakeDamage
CurrentStats.Resources.Health = FMath::Max(0.f, CurrentStats.Resources.Health - Actual);
// SmartNPC 는 NPCAttributes — 멤버 이름도 타입도 다름
```

선택지:

| 안 | 내용 | 평가 |
|---|---|---|
| **A (권장)** | `ICharacterBase` 에 `ApplyResourceDelta(float dHP, float dMana, float dStam)` BlueprintNativeEvent 추가 → VRPawn·SmartNPC 각자 자기 멤버에 적용 | 스탯 쓰기가 **소유자 안에 머문다**(§8 소유권 원칙과 정합). 인터페이스 1개 추가로 끝 |
| B | `UseItem` 은 효과값만 반환, 호출측이 적용 | 컴포넌트는 깨끗하나 호출처(VRPawn·NPCActionComponent)마다 적용 코드 중복 |
| C | `InventoryComponent` 에서 `Cast<AVRPawn>`/`Cast<ASmartNPC>` 분기 | 컴포넌트가 두 구체 클래스에 결합. **채택 금지** |

이하 §4 는 **A 안 기준**으로 기술한다. B 로 바꾸면 §4.2 시그니처만 달라진다.

---

## §3 M1 — 스키마 확장 + 데이터 (선행)

### 3.1 `FItemData` 신규 필드 4개

```cpp
// ItemDataAsset.h — Stats 섹션
// 소비(Consumable) 시 소유자 자원 회복량. 0 이면 해당 자원 미회복.
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Consume",
          meta = (EditCondition = "ItemType == EItemType::Consumable"))
float HealthRestore = 0.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Consume",
          meta = (EditCondition = "ItemType == EItemType::Consumable"))
float ManaRestore = 0.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Consume",
          meta = (EditCondition = "ItemType == EItemType::Consumable"))
float StaminaRestore = 0.f;

// Visual 섹션 — 드랍 스폰·손 부착 공용 메시.
// 비어 있으면 WorldMeshClass(개별 BP) 를 보고, 그것도 없으면 기본 큐브 폴백.
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Visual")
TSoftObjectPtr<UStaticMesh> WorldMesh;
```

### 3.2 CSV 열 4개 추가 + 72행 채우기

`Content/Data/Items/ItemRegistry.csv` 헤더가 14 → **18열**.

소비템 14종 회복값 초안 (Consumable 외 전 행은 `0,0,0`):

| ItemID | HP | Mana | Stam | 근거 |
|---|--:|--:|--:|---|
| `Bread` | 15 | 0 | 25 | 주식 — 포만감이 스태미나로 |
| `Rations` | 20 | 0 | 35 | 여행 비상식, 빵 상위 |
| `WaterSkin` | 5 | 0 | 30 | 갈증 해소 = 스태미나 중심 |
| `Bandage` | 25 | 0 | 0 | 지혈 전용 |
| `HealthPotion` | 50 | 0 | 0 | 표준 회복 |
| `ManaPotion` | 0 | 50 | 0 | 표준 마나 |
| `HerbTea` | 15 | 15 | 10 | 진정 — 소량 복합 |
| `RumBottle` | 0 | 0 | 40 | 사기 진작 = 스태미나 |
| `HolyWater` | 40 | 20 | 0 | 신성 치유 |
| `HerbExtract` | 30 | 10 | 0 | 농축 원료 |
| `Antidote` | 10 | 0 | 0 | 해독 — 상태이상 미구현이라 소량 HP 만 |
| `SmokeVial` | 0 | 0 | 0 | 투척용, 회복 없음 |
| `SmokeBomb` | 0 | 0 | 0 | 동일 |
| `SpellScroll` | 0 | 0 | 0 | 마법 발동, 회복 없음 |

> 회복 0 인 소비템(`SmokeBomb` 등)은 **효과가 없는 게 아니라 이 SPEC 범위 밖의 효과**를 가진다. `UseItem` 은 회복 0 이어도 성공 처리하고 소모시킨다(몽타주·소리는 재생).

`WorldMesh` 는 **전 행 공란으로 시작** — 에셋 확보 전까지 기본 큐브 폴백으로 동작한다(§5.3).

### 3.3 주입

`SPEC_item_registry.md` §6.3 절차 그대로. **함정 2개 재확인 필수**:
- `fill_data_table_from_csv_string` 반환값은 에러 리스트가 아니라 **성공 bool**
- enum·신규 열 파싱 실패는 **조용히 기본값으로 강등** → 저장 전 엔진 내 분포·표본값을 CSV 와 대조

---

## §4 M2 — UseItem (스탯 효과)

### 4.1 `ICharacterBase` 확장 (§2.3 A 안)

```cpp
// Entity.h
// 자원 증감. 소비 아이템·회복 효과의 유일한 쓰기 진입점.
// 구현체가 자기 멤버(CurrentStats / NPCAttributes)에 적용하고 Max 로 클램프한다.
UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Character|Attributes")
void ApplyResourceDelta(float DeltaHealth, float DeltaMana, float DeltaStamina);
```

- `AVRPawn` — `CurrentStats.Resources` 에 적용, `MaxHealth`/`MaxMana`/`MaxStamina` 클램프.
- `ASmartNPC` — `NPCAttributes.Resources` 에 동일.
- **사망 상태 가드**: `Health <= 0` 이면 회복 무시(부활은 별도 경로).

### 4.2 `UInventoryComponent::UseItem`

```cpp
UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
bool UseItem(const FString& ItemID);
```

순서 — **`GiveItem` 증발 버그의 교훈대로 차감 전에 전부 확보**:

1. `GetSlotIndexByItemID` 로 슬롯 확인. 없으면 `false`.
2. 슬롯의 `ItemData` 복사 확보(차감 후엔 못 읽는다).
3. `ItemType != Consumable` 이면 `false` + 경고 로그(장비를 먹으려는 시도).
4. 소유자에서 `ICharacterBase` 조회. 없으면 `false`(차감하지 않음).
5. `RemoveItem(ItemID, 1)` 성공 시에만 `ApplyResourceDelta(H, M, S)` 호출.
6. `OnInventoryChanged` 브로드캐스트 → HUD 자동 갱신.

### 4.3 호출처 연동

| 호출처 | 변경 |
|---|---|
| `NPCActionComponent::ExecuteUseItem` | `RemoveItem` 직접 호출 → `InventoryComponent->UseItem(ItemID)` 로 교체. 몽타주(`Eat`)·소음 이벤트는 **성공 시에만** 재생 |
| `AVRPawn` | 플레이어 사용 입력은 **이 SPEC 범위 밖**(인벤토리 UI 슬롯 클릭 = WBP 작업). C++ `UseItem` 만 열어두고 WBP 가 호출 |

---

## §5 M3 — DropItem (월드 스폰)

### 5.1 `UInventoryComponent::DropItem`

```cpp
UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
bool DropItem(const FString& ItemID, int32 Amount = 1);
```

1. 슬롯·수량 확인 → `ItemData` 복사 확보.
2. 스폰 위치 계산(§5.2) → `ADroppedItemBase` 스폰. **스폰 실패 시 차감하지 않고 중단**(증발 방지).
3. 스폰된 액터에 `ItemMesh` 설정(§5.3) · `ItemData.ItemTemplateID = ItemID` · `Amount` 대입.
   - ⚠ **`spawn_actor` 직후 인스턴스의 `ItemTemplateID` 가 CDO 값을 안 따라온다**(2026-08-24 실측, DoList 1-3). 스폰 후 반드시 명시적으로 써줄 것.
4. `ItemManager->RegisterDroppedItem(FGuid::NewGuid().ToString(), Actor, ItemID)`.
5. `RemoveItem(ItemID, Amount)` → `OnInventoryChanged`.

### 5.2 스폰 위치

소유자 **전방 100cm · 발밑 높이**(캡슐 하단 + 20cm). 물리 시뮬이 켜져 있어 바닥으로 안착한다.

- 벽에 박히는 것 방지: 전방 지점까지 `LineTraceSingleByChannel` 로 막히면 소유자 발밑에 스폰.

### 5.3 메시 결정 순서

```cpp
UStaticMesh* Mesh = nullptr;
if (!Data.WorldMesh.IsNull())            Mesh = Data.WorldMesh.LoadSynchronous();
if (!Mesh && !Data.WorldMeshClass.IsNull()) { /* 개별 BP 스폰 경로 */ }
if (!Mesh)                                Mesh = DefaultDropMesh;   // 기본 큐브
```

`DefaultDropMesh` 는 `UInventoryComponent` 에 `UPROPERTY(EditDefaultsOnly)` 로 노출하고 C++ 기본값을 `/Engine/BasicShapes/Cube` 로 확정(CLAUDE.md §9 — 바이너리에만 두지 말 것).

### 5.4 `ExecuteDrop` TODO 해소

`NPCActionComponent::ExecuteDrop` 의 `SpawnedItem = nullptr` 블록을 통째로 `InventoryComponent->DropItem(ItemID, 1)` 호출로 대체. `Memo.md` 의 "스텁 3종 TODO 존치" 중 **Drop 항목이 이 SPEC 으로 해소**된다.

---

## §6 M4 — EquipItem 손 소켓 부착

### 6.1 소켓 계약

`VRPawn.h:96` 에 이미 의도가 적혀 있다 — **"무기·아이템은 X_Bot hand 본 소켓(`GetMesh()`)에 부착"**.

| `EEquipmentSlot` | 부착 지점 |
|---|---|
| `MainHand` | `RightHand` (본) |
| `OffHand` | `LeftHand` (본) |
| 그 외(Head/Torso/…) | **이 SPEC 범위 밖** — 방어구 미도입 |

> ⚠ 2026-08-31 정정: 원안의 `hand_rSocket`/`hand_lSocket` 은 **실재하지 않는다**. X_Bot 스켈레톤(`/Game/Core/Mesh/NPC/X_Bot`)에 그 이름의 소켓이 없다(MCP `find_socket` 전부 none). 부착 API 는 소켓이 없으면 동명 **본**을 찾으므로, 기존 `MeleeSphere` 부착(`VRPawn.cpp:87·92`)과 같이 Mixamo 본 이름 `RightHand`/`LeftHand` 를 쓴다.

소켓 이름은 `UInventoryComponent` 에 `UPROPERTY(EditDefaultsOnly)` 로 노출(스켈레톤이 바뀌어도 C++ 수정 없이 대응).

### 6.2 부착·해제

- **부착**: `EquipItem` 성공 직후 — 소유자 `GetMesh()` 에 `UStaticMeshComponent` 를 런타임 생성해 `AttachToComponent(Mesh, KeepRelative, SocketName)`. 메시는 §5.3 과 **동일한 결정 순서**.
- **해제**: `UnequipItem`/`UnequipItemByID` 에서 해당 슬롯의 부착 컴포넌트를 `DestroyComponent()`.
- **추적**: `TMap<EEquipmentSlot, TObjectPtr<UStaticMeshComponent>> AttachedMeshes` 를 컴포넌트 멤버로. 슬롯 교체 시 기존 것을 먼저 파괴(§6.3).

### 6.3 함정

- `EquipItem` 은 이미 **점유 슬롯이면 `UnequipItem` 을 먼저 호출**한다(`InventoryComponent.cpp:336`). 해제 경로에 파괴를 넣으면 교체가 자동으로 처리된다 — 부착 경로에서 중복 파괴를 또 하지 말 것.
- 소유자에 `GetMesh()` 가 없거나(순수 `APawn`) 스켈레톤에 소켓이 없으면 **조용히 스킵하고 경고 로그** — 장착 데이터 자체는 성공 처리한다(비주얼 실패가 게임플레이를 막으면 안 됨).

---

## §7 완료 조건

### 7.1 빌드 (클로드)

```
& "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" `
  UE5_MCP_VREditor Win64 Development -Project="C:\github\UE5_MCP_VR\UE5_MCP_VR.uproject" -WaitMutex
```
exit 0 + 신규 경고 0.

### 7.2 데이터 (클로드, MCP)

- `DT_ItemRegistry` 72행 유지 + 신규 4열 반영 확인
- 소비템 14종의 회복값이 CSV 와 일치(엔진 내 덤프 대조)

### 7.3 PIE (사용자)

1. `Bread` 사용 → HP +15 · 스태미나 +25 · 슬롯 수량 −1 · HUD 갱신
2. `HealthPotion` 사용 → HP +50, 최대치 초과 안 함(클램프)
3. `KnightSword` 장착 → **오른손에 메시 부착**(현재는 큐브 폴백) → 해제 시 사라짐
4. `Shield_Knight` 장착 → 왼손. 검과 동시 장착 가능
5. `MainHand` 에 다른 무기 재장착 → 기존 메시 파괴 후 신규 부착(중복 없음)
6. 아이템 드랍 → 전방 바닥에 액터 생성 · 인벤 차감 · **다시 주울 수 있음**(픽업 경로 왕복)
7. 벽 앞에서 드랍 → 벽 너머로 안 나감
8. NPC 에게 "빵 먹어" → `ExecuteUseItem` → NPC HP 증가 + `Eat` 몽타주
9. NPC 에게 "그거 버려" → 월드에 실제 액터 생성(현재는 로그만 남고 증발)

---

## §8 미결 사항

- [x] **§2.3 스탯 쓰기 경로** — **A안 채택**(2026-08-31). `ICharacterBase::ApplyResourceDelta` 신설, 클램프·사망 가드는 `FGameResources::ApplyDelta` 에 두어 VRPawn·SmartNPC 가 같은 규칙을 공유한다.
- [x] **`WorldMesh` 에셋 확보** — 2026-09-01 완료. 72종 메시·아이콘 임포트 및 DataTable 연결(`e96b5db`), 충돌체는 2026-09-05 컨벡스 분해로 재생성(`7f3cff0`).
- [ ] **플레이어 UseItem 입력 경로** — C++ 진입점 `UInventoryComponent::ActivateItem(ItemID)` 신설 완료(2026-09-05, 소비=UseItem·장비=EquipItem 분기). 남은 건 `WBP_InventorySlot` 버튼 배선(DoList 1-8) — VR 클릭 자체는 `HUDInteractor` 로 이미 동작
- [ ] **상태이상 시스템** — `Antidote` 가 해독할 대상이 없다. 필요해지면 별도 SPEC
