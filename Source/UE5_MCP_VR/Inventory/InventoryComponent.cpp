#include "InventoryComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "ItemManager.h"

// 기본 생성자 (Empty Slot 배열 초기화)
UInventoryComponent::UInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    MaxSlotCapacity = 10;
    MaximumWeightLimit = 50.0f;
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

    int32 RemainingAmount = Amount;

    // 1. 스택 가능한 아이템이면 기존 슬롯에 합치기 시도
    if (ItemData.MaxStack > 1) 
    {
        RemainingAmount = TryStackItemsExisting(ItemData, RemainingAmount);
    }

    // 2. 남은 수량을 빈 슬롯에 채우기
    if (RemainingAmount > 0)
    {
        RemainingAmount = TryStoreInEmptySlots(ItemData, RemainingAmount);
    }

    // 공간 부족 검증
    if (RemainingAmount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Slots Full! Cannot add %d more of %s"), RemainingAmount, *ItemData.ItemID);
        
        // 추가 성공한 만큼만 무게 반영
        float PartialWeightAdded = ItemData.Weight * static_cast<float>(Amount - RemainingAmount);
        CurrentWeight += PartialWeightAdded;

        // 부분 추가라도 슬롯이 변했으면 알림
        if (RemainingAmount < Amount)
        {
            OnInventoryChanged.Broadcast();
        }
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

        int32 AvailableSpace = TargetItem.MaxStack - InventorySlots[StackableSlotIndex].Count;
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

        int32 AmountToAdd = FMath::Min(RemainingAmount, TargetItem.MaxStack);
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
    int32 TotalCount = 0;
    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (!Slot.IsEmpty() && Slot.ItemData.ItemID == ItemID)
        {
            TotalCount += Slot.Count;
        }
    }
    // 장착 중 아이템도 보유로 집계 — CalculateWeight 와 동일 기준.
    // (장착만 하면 HasItem=false 가 되어 거래/퀘스트 판정이 어긋나는 문제 방지)
    for (const auto& Pair : EquipmentSlots)
    {
        const FInventorySlot& Slot = Pair.Value;
        if (!Slot.IsEmpty() && Slot.ItemData.ItemID == ItemID)
        {
            TotalCount += Slot.Count;
        }
    }
    return (TotalCount >= Amount);
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
            if (InventorySlots[i].Count < TargetItem.MaxStack)
            {
                return i;
            }
        }
    }
    return INDEX_NONE;
}

bool UInventoryComponent::RepairItem(const FString& ItemID, float Amount)
{
    int32 SlotIndex = GetSlotIndexByItemID(ItemID);
    if (SlotIndex == INDEX_NONE)
    {
        // 인벤토리에 없으면 장비창 검색 — 장착 중인 아이템도 수리 가능해야 함
        for (auto& Pair : EquipmentSlots)
        {
            FItemData& EquippedData = Pair.Value.ItemData;
            if (EquippedData.IsValidItem() && EquippedData.ItemID == ItemID)
            {
                if (EquippedData.bHasDurability)
                {
                    EquippedData.RepairItem(Amount);
                }
                UE_LOG(LogTemp, Log, TEXT("[Inventory] Repaired (equipped) %s."), *EquippedData.DisplayName.ToString());
                OnInventoryChanged.Broadcast();
                return true;
            }
        }

        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Repair Failed: Item %s not found in inventory."), *ItemID);
        return false;
    }

    FInventorySlot& TargetSlot = InventorySlots[SlotIndex];
    FItemData& TargetItemData = TargetSlot.ItemData;
    
    if (TargetItemData.bHasDurability)
    {
        TargetItemData.RepairItem(Amount);
    }

    CalculateWeight();

    UE_LOG(LogTemp, Log, TEXT("[Inventory] Repaired %s."), *TargetItemData.DisplayName.ToString());
    OnInventoryChanged.Broadcast();
    return true;
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

    // 기존 장비가 있다면 착용 해제 (Swap)
    if (EquipmentSlots.Contains(TargetSlot))
    {
        if (!UnequipItem(TargetSlot))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Inventory] Equip Failed: Cannot unequip existing item."));
            return false;
        }
    }

    // 슬롯 분리: 인벤토리 -> 장비창
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
    
    EquipmentSlots.Add(TargetSlot, NewEquipSlot);
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
