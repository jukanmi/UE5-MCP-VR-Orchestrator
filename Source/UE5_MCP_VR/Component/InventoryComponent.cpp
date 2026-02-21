#include "InventoryComponent.h"

// 기본 생성자 (Empty Slot 배열 초기화)
UInventoryComponent::UInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    MaxSlots = 10;
    MaxWeight = 50.0f;
}

// 초기 아이템 지급
void UInventoryComponent::BeginPlay()
{
    Super::BeginPlay();

    // 슬롯 초기화 (Resize)
    Slots.Empty();
    Slots.SetNum(MaxSlots);

    // Default Items 지급
    for (const TSoftObjectPtr<UItemDataAsset>& SoftItem : DefaultItems)
    {
        if (UItemDataAsset* Item = SoftItem.LoadSynchronous())
        {
            // 기본 수량 1개로 지급
            AddItem(Item, 1);
        }
    }
}

// 아이템 추가 (핵심 로직 1)
bool UInventoryComponent::AddItem(UItemDataAsset* Item, int32 Amount, bool bCheckWeight)
{
    if (!Item || Amount <= 0) return false;

    // 무게 체크
    float TotalWeightToAdd = Item->Weight * Amount;
    if (bCheckWeight && (CurrentWeight + TotalWeightToAdd > MaxWeight))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Weight Limit Exceeded! Current: %.1f, Adding: %.1f, Limit: %.1f"), CurrentWeight, TotalWeightToAdd, MaxWeight);
        return false;
    }

    int32 Remaining = Amount;

    // 1. 스택 가능한 아이템이면 기존 슬롯에 합치기 시도
    if (Item->MaxStack > 1) 
    {
        int32 StackableSlot = FindStackableSlot(Item);
        while (StackableSlot != INDEX_NONE && Remaining > 0)
        {
            // 빈 공간 계산 (MaxStack - CurrentCount)
            int32 Space = Item->MaxStack - Slots[StackableSlot].Count;
            if (Space > 0)
            {
                int32 CanAdd = FMath::Min(Remaining, Space);
                Slots[StackableSlot].Count += CanAdd;
                Remaining -= CanAdd;
            }
            
            if (Remaining > 0) break; // 이번 슬롯 꽉 찼으면 다음 단계(빈 슬롯)로
        }
    }

    // 2. 남은 수량을 빈 슬롯에 채우기
    while (Remaining > 0)
    {
        int32 EmptySlot = FindEmptySlot();
        if (EmptySlot == INDEX_NONE)
        {
            // 공간 부족 (Slots Full) -> 부분적으로라도 추가했으면 성공? 보통은 Transaction 실패가 맞음.
            // 여기서는 이미 추가된 부분은 냅두고 남은건 버림 (혹은 실패 처리)
            UE_LOG(LogTemp, Warning, TEXT("[Inventory] Slots Full! Cannot add %d more of %s"), Remaining, *Item->ItemID);
            
            // 실패 시 성공한 부분만 무게 추가 (Partial Add)
            float PartialWeightAdded = Item->Weight * (Amount - Remaining);
            CurrentWeight += PartialWeightAdded;

            return false;
        }

        // 새 슬롯에 할당
        int32 CanAdd = FMath::Min(Remaining, Item->MaxStack);
        Slots[EmptySlot].ItemData = Item;
        Slots[EmptySlot].Count = CanAdd;
        Remaining -= CanAdd;
    }

    // 무게 갱신 (Incremental Update)
    CurrentWeight += TotalWeightToAdd;
    return true;
}

// 아이템 제거 (핵심 로직 2)
bool UInventoryComponent::RemoveItem(const FString& ItemID, int32 Amount)
{
    if (Amount <= 0) return false;

    int32 RemainingToRemove = Amount;
    float WeightRemoved = 0.0f;

    // 전체 슬롯 순회하며 해당 ID를 가진 아이템 차감
    for (int32 i = 0; i < Slots.Num(); i++)
    {
        if (Slots[i].IsEmpty()) continue;
        if (Slots[i].ItemData->ItemID == ItemID)
        {
            int32 CanRemove = FMath::Min(RemainingToRemove, Slots[i].Count);
            Slots[i].Count -= CanRemove;
            RemainingToRemove -= CanRemove;
            
            // 무게 누적
            WeightRemoved += (Slots[i].ItemData->Weight * CanRemove);

            // 다 썼으면 빈 슬롯 처리
            if (Slots[i].Count <= 0)
            {
                Slots[i].ItemData = nullptr;
                Slots[i].Count = 0;
            }

            if (RemainingToRemove <= 0) break;
        }
    }

    // 무게 갱신 (Incremental Update)
    CurrentWeight -= WeightRemoved;
    // 부동소수점 오차 방지
    if (CurrentWeight < 0.0f) CurrentWeight = 0.0f; 

    return (RemainingToRemove == 0); // 요청 수량을 모두 제거했으면 true
}

int32 UInventoryComponent::FindSlotIndexByItemID(const FString& ItemID) const
{
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        if (!Slots[i].IsEmpty() && Slots[i].ItemData && Slots[i].ItemData->ItemID == ItemID)
        {
            return i;
        }
    }
    return -1;
}

// 아이템 보유 검사
bool UInventoryComponent::HasItem(const FString& ItemID, int32 Amount)
{
    int32 TotalCount = 0;
    for (const FInventorySlot& Slot : Slots)
    {
        if (!Slot.IsEmpty() && Slot.ItemData->ItemID == ItemID)
        {
            TotalCount += Slot.Count;
        }
    }
    return (TotalCount >= Amount);
}

// 무게 재계산 (비쌀 수 있으니 변경 시에만 호출)
void UInventoryComponent::CalculateWeight()
{
    float NewWeight = 0.0f;
    // 1. Inventory Slots
    for (const FInventorySlot& Slot : Slots)
    {
        if (!Slot.IsEmpty())
        {
            NewWeight += (Slot.ItemData->Weight * Slot.Count);
        }
    }
    // 2. Equipment Slots
    for (const auto& Pair : EquipmentSlots)
    {
        const FInventorySlot& Slot = Pair.Value;
        if (!Slot.IsEmpty())
        {
             NewWeight += (Slot.ItemData->Weight * Slot.Count);
        }
    }
    CurrentWeight = NewWeight;
}

// 빈 슬롯 인덱스 찾기 (-1 if full)
int32 UInventoryComponent::FindEmptySlot() const
{
    for (int32 i = 0; i < Slots.Num(); i++)
    {
        if (Slots[i].IsEmpty()) return i;
    }
    return INDEX_NONE;
}

// 스택 가능한(공간 남은) 슬롯 찾기
int32 UInventoryComponent::FindStackableSlot(UItemDataAsset* Item) const
{
    for (int32 i = 0; i < Slots.Num(); i++)
    {
        if (!Slots[i].IsEmpty() && Slots[i].ItemData == Item)
        {
            if (Slots[i].Count < Item->MaxStack)
            {
                return i;
            }
        }
    }
    return INDEX_NONE;
}

// --- Equipment Implementation ---

bool UInventoryComponent::EquipItem(const FString& ItemID, EEquipmentSlot Slot)
{
    // 1. Find Item in Inventory
    int32 SlotIndex = FindSlotIndexByItemID(ItemID);
    if (SlotIndex == -1)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Inventory] Equip Failed: Item %s not found in inventory."), *ItemID);
        return false;
    }

    FInventorySlot& InvSlot = Slots[SlotIndex];
    UItemDataAsset* ItemData = InvSlot.ItemData;
    
    // 2. Validate Slot
    // If Slot is None, try to determine from ItemData
    if (Slot == EEquipmentSlot::None)
    {
        if (ItemData && ItemData->EquipSlot != EEquipmentSlot::None)
        {
            Slot = ItemData->EquipSlot;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[Inventory] Equip Failed: Slot not specified for %s."), *ItemID);
            return false;
        }
    }

    // 3. Unequip existing item in target slot (Swap)
    if (EquipmentSlots.Contains(Slot))
    {
        if (!UnequipItem(Slot))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Inventory] Equip Failed: Could not unequip existing item in slot (Full inventory?)."));
            return false;
        }
    }

    // 4. Move Item: Inventory -> Equipment
    FInventorySlot NewEquipSlot = InvSlot;
    NewEquipSlot.Count = 1; // Equip one

    // Remove from inventory
    if (InvSlot.Count > 1)
    {
        InvSlot.Count--;
    }
    else
    {
        Slots[SlotIndex] = FInventorySlot(); // Clear slot
    }
    
    // 5. Add to Equipment
    EquipmentSlots.Add(Slot, NewEquipSlot);

    UE_LOG(LogTemp, Log, TEXT("[Inventory] Equipped %s to Slot %d"), *ItemID, (int32)Slot);

    // 6. Apply Stats & Visuals (Placeholder)
    // Virtual function hooking might be better here for derived classes.

    return true;
}

bool UInventoryComponent::UnequipItem(EEquipmentSlot Slot)
{
    if (!EquipmentSlots.Contains(Slot)) return false;

    FInventorySlot EquippedSlot = EquipmentSlots[Slot];
    
    // 1. Try to add back to inventory
    // Skip weight check to ensure conservation during swap
    if (!AddItem(EquippedSlot.ItemData, 1, false)) 
    {
         UE_LOG(LogTemp, Warning, TEXT("[Inventory] Unequip Failed: Inventory Full. Cannot unequip %s."), *EquippedSlot.ItemData->ItemID);
         return false;
    }

    // 2. Remove from Equipment
    EquipmentSlots.Remove(Slot);
    
    // 3. Weight Correction
    // AddItem added +Weight.
    // But this item was already contributing to CurrentWeight (via Equipment).
    // So we must subtract -Weight to neutralize the double counting.
    CurrentWeight -= (EquippedSlot.ItemData->Weight * 1);

    UE_LOG(LogTemp, Log, TEXT("[Inventory] Unequipped Slot %d"), (int32)Slot);

    // 4. Remove Stats & Visuals (Placeholder)
    
    return true;
}

bool UInventoryComponent::UnequipItemByID(const FString& ItemID)
{
    // Find which slot has this item
    for (const auto& Pair : EquipmentSlots)
    {
        if (Pair.Value.ItemData && Pair.Value.ItemData->ItemID == ItemID)
        {
            return UnequipItem(Pair.Key);
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[Inventory] Unequip Failed: Item %s is not equipped."), *ItemID);
    return false;
}

FInventorySlot UInventoryComponent::GetEquippedItem(EEquipmentSlot Slot) const
{
    const FInventorySlot* Found = EquipmentSlots.Find(Slot);
    return Found ? *Found : FInventorySlot();
}
