#include "Inventory/Components/InventoryComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Inventory/Subsystems/ItemManager.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Core/Interfaces/Entity.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace
{
    // 드랍 위치 — 소유자 전방 이 거리의 발밑에 스폰한다. 손 닿는 거리이면서 자기 캡슐과 안 겹치는 값.
    constexpr float DropForwardDistance = 100.f;

    // 발밑에서 살짝 띄워 스폰 — 바닥과 겹친 채 물리를 켜면 튕겨 날아간다.
    constexpr float DropGroundClearance = 20.f;
}

// 기본 생성자 (Empty Slot 배열 초기화)
UInventoryComponent::UInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    MaxSlotCapacity = 10;
    MaximumWeightLimit = 50.0f;

    // 드랍 스폰 기본값 — 네이티브 클래스가 생성자에서 물리·충돌·상호작용 구체를 이미 갖춘다.
    DroppedItemClass = ADroppedItemBase::StaticClass();

    // 아이템별 메시 에셋이 확보되기 전까지 전 아이템이 이 큐브로 떨어진다.
    DefaultDropMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
}

// 초기 아이템 지급
void UInventoryComponent::BeginPlay()
{
    Super::BeginPlay();

    // 슬롯 초기화 (Resize)
    InventorySlots.Empty();
    InventorySlots.SetNum(MaxSlotCapacity);

    // Default Items 지급
    UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    UItemManager* ItemManager = GameInstance ? GameInstance->GetSubsystem<UItemManager>() : nullptr;

    if (ItemManager)
    {
        for (const FString& RowName : InitialDefaultItems)
        {
            FItemData OutData;
            if (ItemManager->GetItemDataByID(RowName, OutData))
            {
                // 기본 수량 1개로 지급
                AddItem(OutData, 1);
            }
        }
    }
}

// 아이템 추가 (핵심 로직 1)
bool UInventoryComponent::AddItem(const FItemData& ItemData, int32 Amount, bool bCheckWeight)
{
    if (!ItemData.IsValidItem() || Amount <= 0) return false;

    // 무게 체크
    float TotalWeightToAdd = ItemData.Weight * static_cast<float>(Amount);
    if (bCheckWeight && (CurrentWeight + TotalWeightToAdd > MaximumWeightLimit))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Weight Limit Exceeded! Current: %.1f, Adding: %.1f, Limit: %.1f"), CurrentWeight, TotalWeightToAdd, MaximumWeightLimit);
        return false;
    }

    // 실패 시 되돌리기 위한 스냅샷. 부분 적재 상태로 false 를 내면 호출측이 전부
    // "아무것도 안 들어갔다"로 해석해 원본을 그대로 남기므로 아이템이 복제된다
    // (월드 픽업은 액터를 남기고, NPC 전달은 준 쪽 재고를 유지한다).
    const TArray<FInventorySlot> SlotsBackup = InventorySlots;

    int32 RemainingAmount = Amount;

    // 1. 스택 가능한 아이템이면 기존 슬롯에 합치기 시도
    if (GetEffectiveMaxStack(ItemData) > 1)
    {
        RemainingAmount = TryStackItemsExisting(ItemData, RemainingAmount);
    }

    // 2. 남은 수량을 빈 슬롯에 채우기
    if (RemainingAmount > 0)
    {
        RemainingAmount = TryStoreInEmptySlots(ItemData, RemainingAmount);
    }

    // 공간 부족 검증 — 한 개라도 못 넣으면 전량 취소
    if (RemainingAmount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Slots Full! Cannot add %d of %s (%d 개 부족하여 전량 취소)"),
               Amount, *ItemData.ItemID, RemainingAmount);

        InventorySlots = SlotsBackup;
        return false;
    }

    // 전체 추가 성공
    CurrentWeight += TotalWeightToAdd;
    OnInventoryChanged.Broadcast();
    return true;
}

int32 UInventoryComponent::TryStackItemsExisting(const FItemData& TargetItem, int32 RemainingAmount)
{
    // 빈 공간이 남아있는 기존 슬롯을 우선적으로 찾아 아이템을 최대한 눌러 담습니다.
    while (RemainingAmount > 0)
    {
        int32 StackableSlotIndex = GetStackableSlotIndex(TargetItem);
        if (StackableSlotIndex == INDEX_NONE) 
        {
            break; // 더 이상 채울 수 있는 기존 스택이 없음
        }

        int32 AvailableSpace = GetEffectiveMaxStack(TargetItem) - InventorySlots[StackableSlotIndex].Count;
        int32 AmountToAdd = FMath::Min(RemainingAmount, AvailableSpace);

        InventorySlots[StackableSlotIndex].Count += AmountToAdd;
        RemainingAmount -= AmountToAdd;
    }
    return RemainingAmount;
}

int32 UInventoryComponent::TryStoreInEmptySlots(const FItemData& TargetItem, int32 RemainingAmount)
{
    // 기존 스택에 합치고도 남은 아이템들에 대해 새로운 빈 슬롯을 열어 저장합니다.
    while (RemainingAmount > 0)
    {
        int32 EmptySlotIndex = GetFirstAvailableSlotIndex();
        if (EmptySlotIndex == INDEX_NONE)
        {
            break; // 더 이상 빈 슬롯 없음
        }

        int32 AmountToAdd = FMath::Min(RemainingAmount, GetEffectiveMaxStack(TargetItem));
        InventorySlots[EmptySlotIndex].ItemData = TargetItem;
        InventorySlots[EmptySlotIndex].Count = AmountToAdd;
        
        RemainingAmount -= AmountToAdd;
    }
    return RemainingAmount;
}

// 아이템 제거 (핵심 로직 2)
bool UInventoryComponent::RemoveItem(const FString& ItemID, int32 Amount)
{
    if (Amount <= 0) return false;

    // 보유량을 먼저 확인하지 않으면 부족분만큼 차감한 뒤 false 를 반환한다.
    // 호출부(거래·제작)는 실패로 보고 취소하는데 아이템은 이미 사라진 뒤라 영구 증발한다.
    const int32 Held = GetItemCountInSlots(ItemID);
    if (Held < Amount)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Remove Failed: %s held %d < requested %d"), *ItemID, Held, Amount);
        return false;
    }

    int32 RemainingToRemove = Amount;
    float WeightRemoved = 0.0f;

    // 전체 슬롯 순회하며 해당 ID를 가진 아이템 차감
    for (int32 i = 0; i < InventorySlots.Num(); i++)
    {
        if (InventorySlots[i].IsEmpty()) continue;
        
        if (InventorySlots[i].ItemData.ItemID == ItemID)
        {
            int32 AmountToRemove = FMath::Min(RemainingToRemove, InventorySlots[i].Count);
            InventorySlots[i].Count -= AmountToRemove;
            RemainingToRemove -= AmountToRemove;
            
            // 무게 누적 차감
            WeightRemoved += (InventorySlots[i].ItemData.Weight * static_cast<float>(AmountToRemove));

            // 슬롯이 완전히 비워짐
            if (InventorySlots[i].Count <= 0)
            {
                InventorySlots[i].ItemData = FItemData();
                InventorySlots[i].Count = 0;
            }

            if (RemainingToRemove <= 0) 
            {
                break;
            }
        }
    }

    // 무게 갱신 방어 코드 적용
    CurrentWeight = FMath::Max(0.0f, CurrentWeight - WeightRemoved);

    // 일부라도 제거됐으면 알림
    if (RemainingToRemove < Amount)
    {
        OnInventoryChanged.Broadcast();
    }

    return (RemainingToRemove == 0); // 요청 수량을 모두 제거했으면 참
}

// --- 수량 조회 ---

int32 UInventoryComponent::GetEffectiveMaxStack(const FItemData& Item) const
{
    // 장비·내구도 아이템은 개체마다 상태가 달라 겹치면 안 된다. CSV 의 MaxStack 이 잘못 들어와도 막는다.
    if (Item.ItemType == EItemType::Equipment || Item.bHasDurability) return 1;

    // 아이템 데이터가 1(장비 등)이면 그쪽이 이긴다 — 상한은 늘리는 값이 아니라 누르는 값이다.
    return FMath::Max(1, FMath::Min(Item.MaxStack, MaxStackLimit));
}

int32 UInventoryComponent::GetItemCountInSlots(const FString& ItemID) const
{
    int32 Total = 0;
    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (!Slot.IsEmpty() && Slot.ItemData.ItemID == ItemID)
        {
            Total += Slot.Count;
        }
    }
    return Total;
}

int32 UInventoryComponent::GetItemCount(const FString& ItemID) const
{
    int32 Total = GetItemCountInSlots(ItemID);
    for (const auto& Pair : EquipmentSlots)
    {
        const FInventorySlot& Slot = Pair.Value;
        if (!Slot.IsEmpty() && Slot.ItemData.ItemID == ItemID)
        {
            Total += Slot.Count;
        }
    }
    return Total;
}

int32 UInventoryComponent::GetUsedSlotCount() const
{
    int32 Used = 0;
    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (!Slot.IsEmpty()) ++Used;
    }
    return Used;
}

int32 UInventoryComponent::GetFreeSlotCount() const
{
    return InventorySlots.Num() - GetUsedSlotCount();
}

int32 UInventoryComponent::GetTotalItemCount() const
{
    int32 Total = 0;
    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (!Slot.IsEmpty()) Total += Slot.Count;
    }
    return Total;
}

int32 UInventoryComponent::GetRemainingCapacityFor(const FItemData& Item) const
{
    if (!Item.IsValidItem()) return 0;

    const int32 StackLimit = GetEffectiveMaxStack(Item);

    int32 Capacity = 0;
    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (Slot.IsEmpty())
        {
            Capacity += StackLimit;
        }
        else if (Slot.ItemData.ItemID == Item.ItemID)
        {
            Capacity += FMath::Max(0, StackLimit - Slot.Count);
        }
    }
    return Capacity;
}

// 아이템 사용 (소비 효과)
bool UInventoryComponent::UseItem(const FString& ItemID)
{
    const int32 SlotIndex = GetSlotIndexByItemID(ItemID);
    if (SlotIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Use Failed: Item %s not found."), *ItemID);
        return false;
    }

    // 차감하면 슬롯이 비어 회복량을 읽을 수 없다 — 값으로 먼저 복사해 둔다.
    const FItemData Data = InventorySlots[SlotIndex].ItemData;

    if (Data.ItemType != EItemType::Consumable)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Use Failed: %s is not consumable."), *ItemID);
        return false;
    }

    AActor* OwnerActor = GetOwner();
    if (!IsValid(OwnerActor) || !OwnerActor->GetClass()->ImplementsInterface(UCharacterBase::StaticClass()))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Use Failed: Owner cannot receive resources (%s)."), *ItemID);
        return false;
    }

    // 차감 성공 후에만 효과 적용 — 순서가 바뀌면 실패 시 회복만 공짜로 남는다.
    if (!RemoveItem(ItemID, 1)) return false;

    ICharacterBase::Execute_ApplyResourceDelta(OwnerActor, Data.HealthRestore, Data.ManaRestore, Data.StaminaRestore);

    UE_LOG(LogTemp, Log, TEXT("[Inventory] Used %s — HP %+.0f / MP %+.0f / SP %+.0f"),
           *ItemID, Data.HealthRestore, Data.ManaRestore, Data.StaminaRestore);
    return true;
}

void UInventoryComponent::SetSelectedSlot(int32 NewIndex)
{
    const int32 Num = InventorySlots.Num();
    if (Num <= 0) return;

    // 끝에서 반대편으로 돌아온다 — 칸 수가 적어 경계에서 막히면 조작이 답답하다.
    const int32 Wrapped = ((NewIndex % Num) + Num) % Num;
    if (Wrapped == SelectedSlotIndex) return;

    SelectedSlotIndex = Wrapped;
    OnInventoryChanged.Broadcast();
}

// 슬롯 클릭 진입점 — 종류를 보고 사용/장착으로 넘긴다 (이미 장착 중인 장비는 해제)
bool UInventoryComponent::ActivateItem(const FString& ItemID, EEquipmentSlot HandSlot)
{
    // 이미 장착된 장비라면 즉시 해제(토글)
    const EEquipmentSlot EquippedSlot = GetSlotOfEquippedItem(ItemID);
    if (EquippedSlot != EEquipmentSlot::None)
    {
        UE_LOG(LogTemp, Log, TEXT("[Inventory] 이미 장착된 %s 해제 시도 (슬롯 %d)"), *ItemID, (int32)EquippedSlot);
        return UnequipItem(EquippedSlot);
    }

    const int32 SlotIndex = GetSlotIndexByItemID(ItemID);
    if (SlotIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Activate Failed: Item %s not found."), *ItemID);
        return false;
    }

    switch (InventorySlots[SlotIndex].ItemData.ItemType)
    {
    case EItemType::Consumable:
        return UseItem(ItemID);

    // 누른 손에 장착한다 — 왼손 그립이면 OffHand, 오른손 그립이면 MainHand.
    case EItemType::Equipment:
        return EquipItem(ItemID, HandSlot);

    // Quest 는 손에 꺼내면 던져서 버릴 수 있게 되므로 제외한다(DropItem 도 같은 이유로 막는다).
    case EItemType::General:
    {
        AActor* OwnerActor = GetOwner();
        if (IsValid(OwnerActor) && OwnerActor->GetClass()->ImplementsInterface(UPlayerBase::StaticClass()))
        {
            // 인터페이스는 손을 못 받는다(계약 고정) — 오른손이 아니면 컴포넌트에서 직접 꺼낸다.
            if (HandSlot == EEquipmentSlot::MainHand)
            {
                return IPlayerBase::Execute_TakeItemInHand(OwnerActor, ItemID);
            }

            const ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerActor);
            const USkeletalMeshComponent* OwnerMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
            const FTransform HandTransform = OwnerMesh
                ? OwnerMesh->GetSocketTransform(GetSocketNameForSlot(HandSlot))
                : OwnerActor->GetActorTransform();

            return TakeItemToHand(ItemID, HandTransform, HandSlot);
        }
        UE_LOG(LogTemp, Log, TEXT("[Inventory] Activate: %s — 소유자가 손에 들 수 없음."), *ItemID);
        return false;
    }

    default:
        UE_LOG(LogTemp, Log, TEXT("[Inventory] Activate: %s has no action (Quest)."), *ItemID);
        return false;
    }
}

// 월드 액터 스폰 공용 경로 — 드랍(발밑)·손에 꺼내기(손)가 같은 절차를 쓴다.
ADroppedItemBase* UInventoryComponent::SpawnItemActor(const FItemData& Data, const FString& ItemID,
                                                      const FTransform& SpawnTransform, int32 Amount)
{
    AActor* OwnerActor = GetOwner();
    UWorld* World = GetWorld();
    if (!IsValid(OwnerActor) || !World) return nullptr;

    // 스폰 클래스: 아이템이 고유 BP(WorldMeshClass)를 지정했으면 그쪽이 우선.
    // 단 그 BP 가 ADroppedItemBase 파생이 아니면 ItemManager 등록·픽업 경로를 못 타므로 무시한다.
    UClass* SpawnClass = DroppedItemClass ? DroppedItemClass.Get() : ADroppedItemBase::StaticClass();
    bool bUsingItemOwnBlueprint = false;
    if (!Data.WorldMeshClass.IsNull())
    {
        if (UClass* CustomClass = Data.WorldMeshClass.LoadSynchronous())
        {
            if (CustomClass->IsChildOf(ADroppedItemBase::StaticClass()))
            {
                SpawnClass = CustomClass;
                bUsingItemOwnBlueprint = true;
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[Inventory] %s 의 WorldMeshClass 가 ADroppedItemBase 파생이 아니라 무시합니다."), *ItemID);
            }
        }
    }

    // Deferred 스폰 — ADroppedItemBase::BeginPlay 가 ItemTemplateID 로 ItemManager 에 등록한다.
    // FinishSpawning 전에 ID 를 넣지 않으면 "DefaultEntity_Unknown" 으로 등록돼 NPC·픽업이 종류를 모른다.
    ADroppedItemBase* Spawned = World->SpawnActorDeferred<ADroppedItemBase>(
        SpawnClass, SpawnTransform, OwnerActor, nullptr,
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

    if (!Spawned) return nullptr;

    Spawned->ItemData.ItemTemplateID = ItemID;
    Spawned->Amount = Amount;

    // 고유 BP 는 자기 메시를 이미 갖고 있으므로 덮어쓰지 않는다.
    if (!bUsingItemOwnBlueprint && Spawned->ItemMesh)
    {
        if (UStaticMesh* Mesh = ResolveItemMesh(Data))
        {
            Spawned->ItemMesh->SetStaticMesh(Mesh);
        }
    }

    Spawned->FinishSpawning(SpawnTransform);
    return Spawned;
}

// 아이템 드랍 (월드 스폰)
bool UInventoryComponent::DropItem(const FString& ItemID, int32 Amount)
{
    if (Amount <= 0) return false;

    // 장착분을 세는 HasItem 으로 판정하면 슬롯에 없는 수량을 있다고 보고,
    // 뒤이은 RemoveItem 이 일부만 차감한 채 실패해 스폰분과 어긋난다.
    const int32 SlotIndex = GetSlotIndexByItemID(ItemID);
    if (SlotIndex == INDEX_NONE || GetItemCountInSlots(ItemID) < Amount)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Drop Failed: %s x%d not held in slots."), *ItemID, Amount);
        return false;
    }

    const FItemData Data = InventorySlots[SlotIndex].ItemData;

    // Quest 는 정의상 버릴 수 없는 스토리 필수 아이템이다.
    if (Data.ItemType == EItemType::Quest)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Drop Rejected: quest item %s"), *ItemID);
        return false;
    }

    AActor* OwnerActor = GetOwner();
    UWorld* World = GetWorld();
    if (!IsValid(OwnerActor) || !World) return false;

    // 스폰 지점: 소유자 전방 발밑. 벽 너머로 던져지지 않게 트레이스로 막힘을 확인한다.
    const ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerActor);
    const float CapsuleHalfHeight = (OwnerCharacter && OwnerCharacter->GetCapsuleComponent())
        ? OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;

    const FVector FeetLocation = OwnerActor->GetActorLocation()
        - FVector(0.f, 0.f, CapsuleHalfHeight - DropGroundClearance);
    FVector SpawnLocation = FeetLocation + OwnerActor->GetActorForwardVector() * DropForwardDistance;

    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(InventoryDrop), false, OwnerActor);
    FHitResult BlockingHit;
    if (World->LineTraceSingleByChannel(BlockingHit, FeetLocation, SpawnLocation, ECC_WorldStatic, TraceParams))
    {
        SpawnLocation = FeetLocation;
    }

    ADroppedItemBase* Dropped = SpawnItemActor(Data, ItemID,
        FTransform(FRotator::ZeroRotator, SpawnLocation), Amount);

    if (!Dropped)
    {
        // 스폰 실패 시 차감하지 않는다 — 여기서 먼저 지우면 아이템이 증발한다.
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Drop Failed: spawn failed for %s."), *ItemID);
        return false;
    }

    if (!RemoveItem(ItemID, Amount))
    {
        // 위 HasItem 검사를 통과했다면 도달 불가. 도달했다면 스폰분을 되돌려 복제를 막는다.
        UE_LOG(LogTemp, Error, TEXT("[Inventory] Drop 차감 실패 — 스폰한 %s 를 되돌립니다."), *ItemID);
        Dropped->Destroy();
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[Inventory] Dropped %s x%d"), *ItemID, Amount);
    return true;
}

int32 UInventoryComponent::GetSlotIndexByItemID(const FString& ItemID) const
{
    for (int32 i = 0; i < InventorySlots.Num(); ++i)
    {
        if (!InventorySlots[i].IsEmpty() && InventorySlots[i].ItemData.ItemID == ItemID)
        {
            return i;
        }
    }
    return INDEX_NONE;
}

// 아이템 보유 검사
bool UInventoryComponent::HasItem(const FString& ItemID, int32 Amount)
{
    // 장착 중 아이템도 보유로 집계 — CalculateWeight 와 동일 기준.
    // (장착만 하면 HasItem=false 가 되어 거래/퀘스트 판정이 어긋나는 문제 방지)
    // 슬롯에서 실제로 빼내야 하는 동작은 이걸 쓰면 안 된다 — GetItemCountInSlots 로 볼 것.
    return GetItemCount(ItemID) >= Amount;
}

// 무게 재계산 크로스체크용
void UInventoryComponent::CalculateWeight()
{
    float NewWeight = 0.0f;
    // 1. Inventory Slots
    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (!Slot.IsEmpty())
        {
            NewWeight += (Slot.ItemData.Weight * static_cast<float>(Slot.Count));
        }
    }
    // 2. Equipment Slots
    for (const auto& Pair : EquipmentSlots)
    {
        const FInventorySlot& Slot = Pair.Value;
        if (!Slot.IsEmpty())
        {
             NewWeight += (Slot.ItemData.Weight * static_cast<float>(Slot.Count));
        }
    }
    CurrentWeight = NewWeight;
}

// 빈 슬롯 인덱스 찾기 (-1 if full)
int32 UInventoryComponent::GetFirstAvailableSlotIndex() const
{
    for (int32 i = 0; i < InventorySlots.Num(); i++)
    {
        if (InventorySlots[i].IsEmpty()) return i;
    }
    return INDEX_NONE;
}

// 스택 가능한(공간 남은) 슬롯 찾기
int32 UInventoryComponent::GetStackableSlotIndex(const FItemData& TargetItem) const
{
    for (int32 i = 0; i < InventorySlots.Num(); i++)
    {
        if (!InventorySlots[i].IsEmpty() && InventorySlots[i].ItemData.ItemID == TargetItem.ItemID)
        {
            if (InventorySlots[i].Count < GetEffectiveMaxStack(TargetItem))
            {
                return i;
            }
        }
    }
    return INDEX_NONE;
}

// --- Equipment Implementation ---

bool UInventoryComponent::EquipItem(const FString& ItemID, EEquipmentSlot TargetSlot)
{
    int32 SlotIndex = GetSlotIndexByItemID(ItemID);
    if (SlotIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Equip Failed: Item %s not found."), *ItemID);
        return false;
    }

    FInventorySlot& TargetInvSlot = InventorySlots[SlotIndex];
    FItemData& TargetItemData = TargetInvSlot.ItemData;
    
    // 타겟 슬롯 검증
    if (TargetSlot == EEquipmentSlot::None)
    {
        if (TargetItemData.IsValidItem() && TargetItemData.EquipSlot != EEquipmentSlot::None)
        {
            TargetSlot = TargetItemData.EquipSlot;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[Inventory] Equip Failed: Slot not specified for %s."), *ItemID);
            return false;
        }
    }

    // 슬롯 분리를 먼저 한다. 벗은 장비는 UnequipItem 이 인벤토리로 되돌리는데,
    // 슬롯이 꽉 찬 상태(10/10)에서 벗기부터 하면 되돌릴 자리가 없어 교체가 통째로 실패한다.
    const FInventorySlot OriginalSlot = TargetInvSlot;
    FInventorySlot NewEquipSlot = TargetInvSlot;
    NewEquipSlot.Count = 1; // 1개 착용

    if (TargetInvSlot.Count > 1)
    {
        TargetInvSlot.Count--;
    }
    else
    {
        InventorySlots[SlotIndex] = FInventorySlot(); // 슬롯 클리어
    }

    // 기존 장비가 있다면 착용 해제 (Swap) — 위에서 비운 자리로 돌아온다.
    if (EquipmentSlots.Contains(TargetSlot))
    {
        if (!UnequipItem(TargetSlot))
        {
            InventorySlots[SlotIndex] = OriginalSlot; // 분리 취소
            UE_LOG(LogTemp, Warning, TEXT("[Inventory] Equip Failed: Cannot unequip existing item."));
            return false;
        }
    }

    EquipmentSlots.Add(TargetSlot, NewEquipSlot);
    AttachEquipmentMesh(TargetSlot, NewEquipSlot.ItemData);
    UE_LOG(LogTemp, Log, TEXT("[Inventory] Equipped %s to Slot %d"), *ItemID, (int32)TargetSlot);

    OnInventoryChanged.Broadcast();
    return true;
}

bool UInventoryComponent::UnequipItem(EEquipmentSlot TargetSlot)
{
    if (!EquipmentSlots.Contains(TargetSlot)) return false;

    FInventorySlot EquippedSlot = EquipmentSlots[TargetSlot];

    // 1. 인벤토리에 다시 반환 (무게 체크 생략) — Count 전량 반환.
    //    EquipItem 이 현재 1개 고정이지만, 수량 하드코딩은 불변식 위반 시 아이템 증발/무게 드리프트.
    if (!AddItem(EquippedSlot.ItemData, EquippedSlot.Count, false))
    {
         UE_LOG(LogTemp, Warning, TEXT("[Inventory] Unequip Failed: Inventory Full. Cannot unequip %s."), *EquippedSlot.ItemData.ItemID);
         return false;
    }

    EquipmentSlots.Remove(TargetSlot);
    DetachEquipmentMesh(TargetSlot);

    // 2. 무게 이중 계산 보정 (AddItem에서 올라간 무게 상쇄)
    // Count 기준 — EquipItem 이 현재 1개 고정이지만 하드코딩 1.0f 는 불변식 위반 시 무게 드리프트
    CurrentWeight -= (EquippedSlot.ItemData.Weight * static_cast<float>(EquippedSlot.Count));

    UE_LOG(LogTemp, Log, TEXT("[Inventory] Unequipped Slot %d"), (int32)TargetSlot);

    OnInventoryChanged.Broadcast();
    return true;
}

bool UInventoryComponent::UnequipItemByID(const FString& ItemID)
{
    // 키 먼저 확정 후 루프 밖에서 해제 — UnequipItem 이 EquipmentSlots 를 수정(Remove)하므로
    // 순회 중 호출은 이터레이터 무효화 위험(현재는 즉시 return 으로 우연히 안전)
    EEquipmentSlot FoundSlot = EEquipmentSlot::None;
    bool bFound = false;
    for (const auto& Pair : EquipmentSlots)
    {
        if (Pair.Value.ItemData.IsValidItem() && Pair.Value.ItemData.ItemID == ItemID)
        {
            FoundSlot = Pair.Key;
            bFound = true;
            break;
        }
    }

    if (bFound)
    {
        return UnequipItem(FoundSlot);
    }

    UE_LOG(LogTemp, Warning, TEXT("[Inventory] Unequip Failed: Item %s is not equipped."), *ItemID);
    return false;
}

FInventorySlot UInventoryComponent::GetEquippedItem(EEquipmentSlot TargetSlot) const
{
    const FInventorySlot* FoundSlot = EquipmentSlots.Find(TargetSlot);
    return FoundSlot ? *FoundSlot : FInventorySlot();
}

bool UInventoryComponent::IsItemEquipped(const FString& ItemID) const
{
    return GetSlotOfEquippedItem(ItemID) != EEquipmentSlot::None;
}

EEquipmentSlot UInventoryComponent::GetSlotOfEquippedItem(const FString& ItemID) const
{
    for (const auto& Pair : EquipmentSlots)
    {
        if (Pair.Value.ItemData.IsValidItem() && Pair.Value.ItemData.ItemID == ItemID)
        {
            return Pair.Key;
        }
    }
    return EEquipmentSlot::None;
}

ADroppedItemBase* UInventoryComponent::DropEquippedItem(EEquipmentSlot Slot, const FTransform& SpawnTransform)
{
    if (!EquipmentSlots.Contains(Slot)) return nullptr;

    const FInventorySlot EquippedSlot = EquipmentSlots[Slot];
    const FItemData ItemData = EquippedSlot.ItemData;
    const FString ItemID = ItemData.ItemID;
    const int32 Count = EquippedSlot.Count;

    // 1. 월드 액터 스폰
    ADroppedItemBase* Spawned = SpawnItemActor(ItemData, ItemID, SpawnTransform, Count);
    if (!Spawned)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] DropEquippedItem 실패 — 스폰 불가: %s"), *ItemID);
        return nullptr;
    }

    // 2. 장비 슬롯 제거 및 비주얼 메시 파괴
    EquipmentSlots.Remove(Slot);
    DetachEquipmentMesh(Slot);

    // 3. 장비 착용 시 집계되었던 무게 차감
    CurrentWeight = FMath::Max(0.0f, CurrentWeight - (ItemData.Weight * static_cast<float>(Count)));

    UE_LOG(LogTemp, Log, TEXT("[Inventory] DropEquippedItem 성공: %s (슬롯 %d)"), *ItemID, (int32)Slot);
    OnInventoryChanged.Broadcast();

    return Spawned;
}

// --- Visual Implementation ---

UStaticMesh* UInventoryComponent::ResolveItemMesh(const FItemData& Data) const
{
    if (!Data.WorldMesh.IsNull())
    {
        if (UStaticMesh* Mesh = Data.WorldMesh.LoadSynchronous())
        {
            return Mesh;
        }
    }

    return DefaultDropMesh.IsNull() ? nullptr : DefaultDropMesh.LoadSynchronous();
}

FName UInventoryComponent::GetSocketNameForSlot(EEquipmentSlot Slot) const
{
    switch (Slot)
    {
    case EEquipmentSlot::MainHand: return MainHandSocket;
    case EEquipmentSlot::OffHand:  return OffHandSocket;
    default:                       return NAME_None;   // 방어구 부위는 부착 대상 없음
    }
}

// --- 손에 쥐기 ---

ADroppedItemBase* UInventoryComponent::GetHeldItem(EEquipmentSlot HandSlot) const
{
    const TObjectPtr<ADroppedItemBase>* Found = HeldItems.Find(HandSlot);
    return (Found && IsValid(*Found)) ? Found->Get() : nullptr;
}

void UInventoryComponent::AttachItemToHand(ADroppedItemBase* Item, EEquipmentSlot HandSlot)
{
    if (!IsValid(Item) || !Item->ItemMesh) return;

    // 손이 아닌 슬롯(머리·몸통 등)에는 쥘 수 없다 — 소켓이 없어 부착 지점이 정해지지 않는다.
    const FName SocketName = GetSocketNameForSlot(HandSlot);
    if (SocketName.IsNone()) return;

    const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* OwnerMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
    if (!OwnerMesh) return;

    // 물리를 끄고 손 본에 그대로 붙인다. 쥔 동안 콜리전까지 끄는 이유는 물리 바디가 남아 있으면
    // 자기 캡슐·바닥을 밀어 손이 튀거나 소유자가 밀려나기 때문.
    Item->ItemMesh->SetSimulatePhysics(false);
    Item->ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Item->AttachToComponent(OwnerMesh,
        FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);

    // 아이템이 자기 보정값을 갖고 있으면 그쪽이 이긴다 — 여기 값은 아무 값도 없는 아이템용 기본치.
    FVector Offset = DefaultHoldOffset;
    FRotator Rotation = DefaultHoldRotation;

    UItemManager* ItemManager = UItemManager::Get(this);

    FItemData Data;
    if (ItemManager && ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, Data))
    {
        if (!Data.HoldOffset.IsNearlyZero())   Offset = Data.HoldOffset;
        if (!Data.HoldRotation.IsNearlyZero()) Rotation = Data.HoldRotation;
    }

    Item->SetActorRelativeLocation(Offset);
    Item->SetActorRelativeRotation(Rotation);

    HeldItems.Add(HandSlot, Item);
}

ADroppedItemBase* UInventoryComponent::ReleaseHeldItem(EEquipmentSlot HandSlot)
{
    ADroppedItemBase* Item = GetHeldItem(HandSlot);
    HeldItems.Remove(HandSlot);
    if (!IsValid(Item)) return nullptr;

    Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    return Item;
}

bool UInventoryComponent::TakeItemToHand(const FString& ItemID, const FTransform& HandTransform,
                                        EEquipmentSlot HandSlot)
{
    if (ADroppedItemBase* Already = GetHeldItem(HandSlot))
    {
        UE_LOG(LogTemp, Log, TEXT("[Inventory] 꺼내기 실패 — 그 손에 이미 %s 를 쥐고 있음"),
            *Already->ItemData.ItemTemplateID);
        return false;
    }

    UItemManager* ItemManager = UItemManager::Get(this);

    FItemData Data;
    if (!ItemManager || !ItemManager->GetItemDataByID(ItemID, Data))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] 꺼내기 실패 — 아이템 데이터 없음: %s"), *ItemID);
        return false;
    }

    if (GetItemCountInSlots(ItemID) < 1) return false;

    // Quest 는 손에 꺼내는 순간 던져서 버릴 수 있게 된다 — DropItem 이 막는 것과 같은 이유로 막는다.
    if (Data.ItemType == EItemType::Quest)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] 꺼내기 거부 — 퀘스트 아이템: %s"), *ItemID);
        return false;
    }

    ADroppedItemBase* Spawned = SpawnItemActor(Data, ItemID, HandTransform, 1);
    if (!Spawned)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] 꺼내기 실패 — 스폰 실패: %s"), *ItemID);
        return false;
    }

    // 스폰이 끝난 뒤에만 차감한다. 차감 실패 시 스폰분을 되돌려야 복제가 안 생긴다.
    if (!RemoveItem(ItemID, 1))
    {
        Spawned->Destroy();
        return false;
    }

    AttachItemToHand(Spawned, HandSlot);
    UE_LOG(LogTemp, Log, TEXT("[Inventory] 꺼냄: %s"), *ItemID);
    return true;
}

bool UInventoryComponent::StoreHeldItem(EEquipmentSlot HandSlot)
{
    ADroppedItemBase* Item = ReleaseHeldItem(HandSlot);
    if (!Item) return false;

    // 어느 이유로 실패하든 손을 떠난 물건은 물리를 되살려 바닥에 남겨야 한다 — 안 그러면
    // 부착도 물리도 없는 채로 공중에 박제된다.
    auto DropWhereItIs = [Item]()
    {
        if (Item->ItemMesh)
        {
            Item->ItemMesh->SetSimulatePhysics(true);
            Item->ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }
        return false;
    };

    UItemManager* ItemManager = UItemManager::Get(this);

    FItemData Data;
    if (!ItemManager || !ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, Data))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] 수납 실패 — 아이템 데이터 없음: %s"), *Item->ItemData.ItemTemplateID);
        return DropWhereItIs();
    }

    const int32 Amount = FMath::Max(1, Item->Amount);
    if (!AddItem(Data, Amount))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] 가득 참 — %s 수납 불가, 그 자리에 드랍"), *Data.ItemID);
        return DropWhereItIs();
    }

    UE_LOG(LogTemp, Log, TEXT("[Inventory] 손에 쥔 아이템 수납 완료: %s (수량 %d)"), *Data.ItemID, Amount);
    Item->Destroy();
    return true;
}

void UInventoryComponent::AttachEquipmentMesh(EEquipmentSlot Slot, const FItemData& Data)
{
    const FName SocketName = GetSocketNameForSlot(Slot);
    if (SocketName.IsNone()) return;

    // 같은 슬롯에 두 번 부착하면 이전 컴포넌트가 떨어지지 않고 그대로 남는다.
    DetachEquipmentMesh(Slot);

    const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* OwnerMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;

    // 비주얼 실패는 장착 데이터까지 되돌리지 않는다 — 로그만 남기고 넘어간다.
    if (!OwnerMesh || !OwnerMesh->DoesSocketExist(SocketName))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] 장착 메시 부착 생략 — 소켓 %s 없음(%s)."),
               *SocketName.ToString(), *Data.ItemID);
        return;
    }

    UStaticMesh* Mesh = ResolveItemMesh(Data);
    if (!Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] 장착 메시 부착 생략 — 메시 없음(%s)."), *Data.ItemID);
        return;
    }

    UStaticMeshComponent* AttachedMesh = NewObject<UStaticMeshComponent>(GetOwner());
    AttachedMesh->SetStaticMesh(Mesh);

    // 손에 붙은 장식물이 캡슐·NPC 인지를 방해하지 않도록 충돌은 끈다(타격 판정은 MeleeSphere 담당).
    AttachedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AttachedMesh->RegisterComponent();
    AttachedMesh->AttachToComponent(OwnerMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, SocketName);

    // 손안 자세는 손으로 쥘 때와 같은 값을 쓴다 — 장착과 쥐기가 다른 각도로 붙으면
    // 인벤토리에서 꺼내 든 검과 장착한 검이 다른 물건처럼 보인다.
    AttachedMesh->SetRelativeLocation(Data.HoldOffset);
    AttachedMesh->SetRelativeRotation(Data.HoldRotation);

    AttachedMeshes.Add(Slot, AttachedMesh);
}

void UInventoryComponent::DetachEquipmentMesh(EEquipmentSlot Slot)
{
    if (TObjectPtr<UStaticMeshComponent>* Found = AttachedMeshes.Find(Slot))
    {
        if (IsValid(*Found))
        {
            (*Found)->DestroyComponent();
        }
        AttachedMeshes.Remove(Slot);
    }
}
