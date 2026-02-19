// Source/UE5_MCP_VR/Component/NPCInventoryComponent.cpp

#include "NPCInventoryComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// 기본 생성자 (Empty Slot 배열 초기화)
UNPCInventoryComponent::UNPCInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    MaxSlots = 10;
    MaxWeight = 50.0f;
}

// 초기 아이템 지급
void UNPCInventoryComponent::BeginPlay()
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
bool UNPCInventoryComponent::AddItem(UItemDataAsset* Item, int32 Amount)
{
    if (!Item || Amount <= 0) return false;

    // 무게 체크
    float TotalWeightToAdd = Item->Weight * Amount;
    if (CurrentWeight + TotalWeightToAdd > MaxWeight)
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
            
            // 아직 남았다면 다음 스택 가능한 슬롯 찾기 (다른 슬롯에 또 있을리 만무하지만 방어적으로 짬)
            // 실제로는 보통 FindStackableSlot을 여러번 호출하기보다
            // 전체 슬롯을 순회하며 빈 공간을 채우는 방식이 더 안전함.
            // 여기서는 단순화를 위해 생략 혹은 다음 단계(FindEmptySlot)로 넘김.
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
            // 무게 롤백 필요하지만 단순화를 위해 생략
            CalculateWeight(); // 정확한 무게 재계산
            return false;
        }

        // 새 슬롯에 할당
        int32 CanAdd = FMath::Min(Remaining, Item->MaxStack);
        Slots[EmptySlot].ItemData = Item;
        Slots[EmptySlot].Count = CanAdd;
        Remaining -= CanAdd;
    }

    CalculateWeight();
    return true;
}

// 아이템 제거 (핵심 로직 2)
bool UNPCInventoryComponent::RemoveItem(const FString& ItemID, int32 Amount)
{
    if (Amount <= 0) return false;

    int32 RemainingToRemove = Amount;

    // 전체 슬롯 순회하며 해당 ID를 가진 아이템 차감
    // 뒤에서부터 지우는게 안전할 수 있으나, 여기선 앞에서부터 차감
    for (int32 i = 0; i < Slots.Num(); i++)
    {
        if (Slots[i].IsEmpty()) continue;
        if (Slots[i].ItemData->ItemID == ItemID)
        {
            int32 CanRemove = FMath::Min(RemainingToRemove, Slots[i].Count);
            Slots[i].Count -= CanRemove;
            RemainingToRemove -= CanRemove;

            // 다 썼으면 빈 슬롯 처리
            if (Slots[i].Count <= 0)
            {
                Slots[i].ItemData = nullptr;
                Slots[i].Count = 0;
            }

            if (RemainingToRemove <= 0) break;
        }
    }

    CalculateWeight();
    return (RemainingToRemove == 0); // 요청 수량을 모두 제거했으면 true
}

// 아이템 보유 검사
bool UNPCInventoryComponent::HasItem(const FString& ItemID, int32 Amount)
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
void UNPCInventoryComponent::CalculateWeight()
{
    float NewWeight = 0.0f;
    for (const FInventorySlot& Slot : Slots)
    {
        if (!Slot.IsEmpty())
        {
            NewWeight += (Slot.ItemData->Weight * Slot.Count);
        }
    }
    CurrentWeight = NewWeight;
}

// 빈 슬롯 인덱스 찾기 (-1 if full)
int32 UNPCInventoryComponent::FindEmptySlot() const
{
    for (int32 i = 0; i < Slots.Num(); i++)
    {
        if (Slots[i].IsEmpty()) return i;
    }
    return INDEX_NONE;
}

// 스택 가능한(공간 남은) 슬롯 찾기
int32 UNPCInventoryComponent::FindStackableSlot(UItemDataAsset* Item) const
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

// JSON 변환 (LLM Context용)
FString UNPCInventoryComponent::GetInventoryJson() const
{
    // Output Array: [{"id": "Sword", "name": "Iron Sword", "count": 1}, ...]
    TArray<TSharedPtr<FJsonValue>> JsonArray;

    for (const FInventorySlot& Slot : Slots)
    {
        if (!Slot.IsEmpty())
        {
            TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
            JsonObj->SetStringField(TEXT("id"), Slot.ItemData->ItemID);
            JsonObj->SetStringField(TEXT("name"), Slot.ItemData->DisplayName.ToString());
            JsonObj->SetStringField(TEXT("desc"), Slot.ItemData->Description);
            JsonObj->SetNumberField(TEXT("count"), Slot.Count);
            
            // 무게 및 타입 정보도 주면 좋음 (LLM 판단 근거)
            JsonObj->SetNumberField(TEXT("weight"), Slot.ItemData->Weight);
            
            TSharedRef<FJsonValueObject> JsonValue = MakeShareable(new FJsonValueObject(JsonObj));
            JsonArray.Add(JsonValue);
        }
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(JsonArray, Writer);

    return OutputString;
}
