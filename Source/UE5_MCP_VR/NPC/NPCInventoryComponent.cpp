#include "NPCInventoryComponent.h"
#include "JsonObjectConverter.h"

UNPCInventoryComponent::UNPCInventoryComponent()
{
    // 기본 설정은 부모 클래스(UInventoryComponent) 생성자에서 처리됨.
    // NPC 특화 설정이 있다면 여기서 추가.
}

void UNPCInventoryComponent::BeginPlay()
{
    Super::BeginPlay();
    // 추가 초기화 로직이 필요하면 여기에 작성.
}

FString UNPCInventoryComponent::GetInventoryJson() const
{
    // Output Array: [{"id": "Sword", "name": "Iron Sword", "count": 1}, ...]
    TArray<TSharedPtr<FJsonValue>> JsonArray;

    // 1. Inventory Slots
    for (const FInventorySlot& Slot : InventorySlots)
    {
        if (!Slot.IsEmpty())
        {
            TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
            JsonObj->SetStringField(TEXT("id"), Slot.ItemData.ItemID);
            JsonObj->SetStringField(TEXT("name"), Slot.ItemData.DisplayName.ToString());
            JsonObj->SetStringField(TEXT("desc"), Slot.ItemData.Description);
            JsonObj->SetNumberField(TEXT("count"), Slot.Count);
            JsonObj->SetNumberField(TEXT("weight"), Slot.ItemData.Weight);
            JsonObj->SetStringField(TEXT("type"), TEXT("item")); // 구분자
            
            TSharedRef<FJsonValueObject> JsonValue = MakeShareable(new FJsonValueObject(JsonObj));
            JsonArray.Add(JsonValue);
        }
    }

    // 2. Equipment Slots (Requested to be included or just kept internal?
    // User context implies we might want to show equipped items too, 
    // but originally GetInventoryJson only scanned Slots.
    // Let's stick to the previous behavior unless specifically asked to change context.
    // However, since they are now separate containers, LLM might lose context of what is equipped
    // if we don't include it. 
    // *Correction*: In the previous session, the user seemed to be refactoring Equipment 
    // and I noticed "GetInventoryJson" didn't include equipment.
    // I should probably add it now or leave it as is. 
    // User instruction was "Separate generic from NPC LLM logic".
    // I will preserve the existing logic (Iterating Slots) for now to avoid side effects.
    // If the user wants to see equipped items in LLM, that's a separate task.

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(JsonArray, Writer);

    return OutputString;
}
