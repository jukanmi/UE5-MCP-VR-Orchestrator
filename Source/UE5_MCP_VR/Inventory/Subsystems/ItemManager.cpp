#include "Inventory/Subsystems/ItemManager.h"
#include "Engine/World.h"
#include "Core/Utils/SubsystemUtils.h"
#include "Engine/GameInstance.h"
#include "JsonObjectConverter.h" // FJsonObjectConverter용
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "Inventory/BP/ItemDataAsset.h"
#include "Inventory/Types/ItemRegistryOptions.h" // ItemRegistryPaths::DefaultItemTable
#include "Inventory/BP/DroppedItemBase.h"
#include "Engine/DataTable.h"

UItemManager* UItemManager::Get(const UObject* WorldContext)
{
    return SubsystemUtils::GetGameSubsystem<UItemManager>(WorldContext);
}

void UItemManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ActiveDroppedItems.Empty();

    ItemTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, ItemRegistryPaths::DefaultItemTable));
    if (!ItemTable)
    {
        UE_LOG(LogTemp, Error, TEXT("[ItemManager] 아이템 마스터 테이블(%s)을 찾을 수 없습니다 — 모든 GetItemDataByID 가 실패합니다."),
            ItemRegistryPaths::DefaultItemTable);
    }
    UE_LOG(LogTemp, Log, TEXT("[ItemManager] Subsystem Initialized. Ready to track world items."));
}

void UItemManager::Deinitialize()
{
    ActiveDroppedItems.Empty();
    Super::Deinitialize();
}

void UItemManager::RegisterDroppedItem(const FString& InInstanceID, AActor* InItemActor, const FString& InTemplateID)
{
    // 댕글링 포인터 등록 방지 및 ID 유효성 검사로 데이터 무결성을 보장합니다.
    if (!IsValid(InItemActor) || InInstanceID.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[ItemManager] Failed to register: Invalid Actor or Empty Instance ID."));
        return;
    }

    if (ActiveDroppedItems.Contains(InInstanceID))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ItemManager] Instance ID %s is already registered. Overwriting."), *InInstanceID);
    }

    FDroppedItemData NewItemData(InInstanceID, InTemplateID, InItemActor);
    ActiveDroppedItems.Add(InInstanceID, NewItemData);

    // Event Driven 방식의 근간: 이 시점에서 외부(MCP 등)로 이벤트를 발송해 상태 일치를 꾀할 수 있습니다.
    UE_LOG(LogTemp, Log, TEXT("[ItemManager] Registered Item: %s | Template: %s"), *InInstanceID, *InTemplateID);
}

void UItemManager::UnregisterDroppedItem(const FString& InInstanceID)
{
    if (InInstanceID.IsEmpty() || !ActiveDroppedItems.Contains(InInstanceID))
    {
        return;
    }

    ActiveDroppedItems.Remove(InInstanceID);

    // 아이템 소멸/획득 이벤트 전파 시점
    UE_LOG(LogTemp, Log, TEXT("[ItemManager] Unregistered Item: %s"), *InInstanceID);
}

TArray<ADroppedItemBase*> UItemManager::GetItemsInRange(const FVector& SearchLocation, float SearchRadius) const
{
    TArray<ADroppedItemBase*> FoundItems;
    UGameInstance* GI = GetGameInstance();
    UWorld* World = GI ? GI->GetWorld() : nullptr;
    if (!World)
    {
        return FoundItems;
    }

    // TMap 전체 순회를 피하기 위한 스윕(Sweep) 로직입니다. 물리 오브젝트 타입으로 1차 필터링합니다.
    TArray<FOverlapResult> OverlapResults;
    FCollisionObjectQueryParams ObjectQueryParams;
    ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
    ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

    FCollisionShape CollisionSphere = FCollisionShape::MakeSphere(SearchRadius);

    const bool bHasOverlap = World->OverlapMultiByObjectType(
        OverlapResults,
        SearchLocation,
        FQuat::Identity,
        ObjectQueryParams,
        CollisionSphere
    );

    if (!bHasOverlap)
    {
        return FoundItems;
    }

    // 한 액터가 컴포넌트마다 여러 번 잡히므로 중복 제거. 등록 여부는 액터가 든 InstanceID 로 O(1) 확인.
    for (const FOverlapResult& HitResult : OverlapResults)
    {
        ADroppedItemBase* Dropped = Cast<ADroppedItemBase>(HitResult.GetActor());
        if (!IsValid(Dropped) || FoundItems.Contains(Dropped)) continue;
        if (!ActiveDroppedItems.Contains(Dropped->ItemData.ItemInstanceID)) continue;
        FoundItems.Add(Dropped);
    }

    return FoundItems;
}

bool UItemManager::GetItemDataByID(const FString& InTemplateID, FItemData& OutItemData) const
{
    if (InTemplateID.IsEmpty() || !ItemTable) return false;

    // 하나의 마스터 테이블(CSV 한 장)에서 RowName 으로 검색 — 수천 개 에셋 대신 기획 데이터 일괄 관리.
    const FItemData* FoundRow = ItemTable->FindRow<FItemData>(FName(*InTemplateID), TEXT("GetItemDataByID"));
    if (FoundRow)
    {
        OutItemData = *FoundRow; // 원본 데이터를 복사해서 넘겨줍니다. (내구도 등 개별화를 위해 복사가 필수)
        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("[ItemManager] DataTable에 '%s' 아이템을 찾을 수 없습니다."), *InTemplateID);
    return false;
}
