#include "ItemManager.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "JsonObjectConverter.h" // FJsonObjectConverter용
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "BP/ItemDataAsset.h"
#include "ItemRegistryOptions.h" // ItemRegistryPaths::DefaultItemTable

void UItemManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ActiveDroppedItems.Empty();
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

AActor* UItemManager::GetItemActorByID(const FString& InInstanceID) const
{
    const FDroppedItemData* FoundDataPtr = ActiveDroppedItems.Find(InInstanceID);
    if (FoundDataPtr && IsValid(FoundDataPtr->ItemActor))
    {
        return FoundDataPtr->ItemActor;
    }
    return nullptr;
}

TArray<FDroppedItemData> UItemManager::GetItemsInRange(const FVector& SearchLocation, float SearchRadius) const
{
    TArray<FDroppedItemData> FoundItems;
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

    // 중복 처리를 방지하기 위해 TSet으로 한번 거르거나, 순회하며 매니저 소속인지 판별합니다.
    TSet<AActor*> ProcessedActors;

    for (const FOverlapResult& HitResult : OverlapResults)
    {
        AActor* OverlappedActor = HitResult.GetActor();
        
        // 유효성 및 중복 검증
        if (!IsValid(OverlappedActor) || ProcessedActors.Contains(OverlappedActor))
        {
            continue;
        }

        ProcessedActors.Add(OverlappedActor);

        // O(N) 서치를 방지하기 위해 액터 자체 컴포넌트나 인터페이스 등에서 인스턴스 ID를 꺼내와서 Map에서 O(1)로 가져와야 이상적입니다.
        // 현재는 역참조를 위해 (액터 포인터 일치 여부) 최소한의 O(K) 순회를 수행합니다. K는 오버랩된 액터 수.
        // 추후 AInteractableItem 같은 베이스 클래스에 ID 멤버가 있다면 캐스트하여 키로 사용하면 훨씬 빠릅니다.
        
        for (const auto& Pair : ActiveDroppedItems)
        {
            if (Pair.Value.ItemActor == OverlappedActor)
            {
                FoundItems.Add(Pair.Value);
                break; // 액터를 찾았으므로 다음 HitResult로 이동
            }
        }
    }

    return FoundItems;
}

FString UItemManager::SerializeActiveItemsToJson() const
{
    // 현재 매니저가 캐싱 중인 데이터를 MCP 전송용 규격에 맞춰 직렬화합니다.
    // TSharedPtr<FJsonObject> 등을 통해 {"type": "world_items_state", "data": [...]} 형태를 구성할 수 있습니다.
    
    // 단순 디버그용 덤프 예시입니다. (실 적용 시 JsonObject 생성 로직 구비)
    int32 Count = ActiveDroppedItems.Num();
    FString ResultStr = FString::Printf(TEXT("{\"type\": \"world_items_state\", \"count\": %d, \"data\": []}"), Count);
    
    // TODO: FJsonObjectConverter나 FJsonSerializer를 사용해 규격화된 Json String 반환 로직 구성.
    
    return ResultStr;
}

bool UItemManager::GetItemDataByID(const FString& InTemplateID, FItemData& OutItemData) const
{
    if (InTemplateID.IsEmpty()) return false;
    
    //  하나의 거대 데이터 테이블(DataTable)에서 RowName을 기반으로 아이템 정보를 빠르게 검색합니다.
    // 수천 개의 에셋 파일을 만드는 대신 엑셀이나 CSV 하나로 기획 데이터를 일괄 관리할 수 있습니다.
    if (!GlobalItemDataTable)
    {
        // 최후의 수단: 만약 블루프린트에서 세팅이 안되었다면, 기본 컨벤션 경로에서 동적으로 로드 시도
        UDataTable* LoadedTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, ItemRegistryPaths::DefaultItemTable));
        if (!LoadedTable)
        {
            UE_LOG(LogTemp, Error, TEXT("[ItemManager] GlobalItemDataTable이 설정되지 않았으며, 기본 경로(/Game/Data/Items/DT_ItemRegistry)에서도 테이블을 찾을 수 없습니다!"));
            return false;
        }
        // const 제거 우회 대신 로컬 변수 사용(const 함수 내부이므로 멤버 변수 할당 불가하여 로컬 테이블로 참조)
        FItemData* FoundRow = LoadedTable->FindRow<FItemData>(FName(*InTemplateID), TEXT("GetItemDataByID"));
        if (FoundRow)
        {
            OutItemData = *FoundRow;
            return true;
        }
        return false;
    }

    // 통상적인 로직: 세팅된 데이터 테이블에서 찾기
    FItemData* FoundRow = GlobalItemDataTable->FindRow<FItemData>(FName(*InTemplateID), TEXT("GetItemDataByID"));
    if (FoundRow)
    {
        OutItemData = *FoundRow; // 원본 데이터를 복사해서 넘겨줍니다. (내구도 등 개별화를 위해 복사가 필수)
        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("[ItemManager] DataTable에 '%s' 아이템을 찾을 수 없습니다."), *InTemplateID);
    return false;
}
