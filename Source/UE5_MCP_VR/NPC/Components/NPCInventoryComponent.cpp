#include "NPC/Components/NPCInventoryComponent.h"
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

    // 2. Equipment Slots — 장착 중인 아이템은 InventorySlots 에서 빠져나가 있어
    // 여기를 순회하지 않으면 LLM 컨텍스트에서 완전히 사라진다("들고 있는 검을 잊는다").
    for (const auto& Pair : EquipmentSlots)
    {
        const FInventorySlot& EqSlot = Pair.Value;
        if (EqSlot.IsEmpty()) continue;

        TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
        JsonObj->SetStringField(TEXT("id"), EqSlot.ItemData.ItemID);
        JsonObj->SetStringField(TEXT("name"), EqSlot.ItemData.DisplayName.ToString());
        JsonObj->SetStringField(TEXT("desc"), EqSlot.ItemData.Description);
        JsonObj->SetNumberField(TEXT("count"), EqSlot.Count);
        JsonObj->SetNumberField(TEXT("weight"), EqSlot.ItemData.Weight);
        JsonObj->SetStringField(TEXT("type"), TEXT("item"));
        JsonObj->SetBoolField(TEXT("equipped"), true);

        JsonArray.Add(MakeShareable(new FJsonValueObject(JsonObj)));
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(JsonArray, Writer);

    return OutputString;
}
