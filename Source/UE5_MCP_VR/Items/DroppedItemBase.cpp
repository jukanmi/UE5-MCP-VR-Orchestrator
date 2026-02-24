#include "DroppedItemBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "ItemManager.h"
#include "Engine/GameInstance.h"

ADroppedItemBase::ADroppedItemBase()
{
    // 월드에 떨어진 단순 물리 아이템은 틱(Tick) 단위의 감시가 굳이 필요 없으므로, 성능 최적화를 위해 Ticking을 끕니다.
    PrimaryActorTick.bCanEverTick = false; 

    // 1. 물리/시각적 형태 설정
    ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
    RootComponent = ItemMesh;
    ItemMesh->SetSimulatePhysics(true);
    
    // NPC 쿼리(OverlapMultiByObjectType) 시 최우선 탐지 대상(PhysicsBody)이 되게 맞춰줍니다.
    ItemMesh->SetCollisionObjectType(ECC_PhysicsBody); 
    ItemMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    
    // 2. 상호작용 감지용 구체 반경 설정
    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    InteractionSphere->SetupAttachment(RootComponent);
    InteractionSphere->SetSphereRadius(100.f); 
    InteractionSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
}

void ADroppedItemBase::BeginPlay()
{
    Super::BeginPlay();

    // 스폰 성공 시 즉시 중복되지 않는 고유 UUID를 발급하여 글로벌 관리를 돕습니다.
    ItemData.ItemInstanceID = FGuid::NewGuid().ToString();
    ItemData.ItemActor = this; // 안전한 검증용으로 자기 자신의 포인터 참조를 들고 있게 합니다.

    // 예외 처리: 월드에 직접 배치하는 등 TemplateID를 깜빡했을 경우를 위한 방어 코드
    if (ItemData.ItemTemplateID.IsEmpty())
    {
        ItemData.ItemTemplateID = TEXT("DefaultEntity_Unknown");
    }

    // ItemManager 서브시스템 검출 및 안전한 초기 등록
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UItemManager* ItemManager = GameInstance->GetSubsystem<UItemManager>())
        {
            ItemManager->RegisterDroppedItem(ItemData.ItemInstanceID, ItemData.ItemActor, ItemData.ItemTemplateID);
        }
    }
}

void ADroppedItemBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 맵에서 사라지는 어떤 경우(파괴, 레벨 언로드 등)에도 댕글링 포인터를 유발하지 않고 매니저 캐시에서 삭제되게 합니다.
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UItemManager* ItemManager = GameInstance->GetSubsystem<UItemManager>())
        {
            ItemManager->UnregisterDroppedItem(ItemData.ItemInstanceID);
        }
    }

    Super::EndPlay(EndPlayReason);
}

void ADroppedItemBase::ConsumeItem()
{
    // 플레이어나 NPC가 아이템을 먹고 맵에서 없앨 때를 위한 표준 래퍼 함수입니다. 파괴 시 자동으로 EndPlay가 호출되어 안전이 보장됩니다.
    UE_LOG(LogTemp, Log, TEXT("[DroppedItemBase] %s 아이템이 획득/소모되어 월드에서 파괴됩니다."), *ItemData.ItemTemplateID);
    Destroy();
}
