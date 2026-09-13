#include "Inventory/BP/DroppedItemBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Inventory/Subsystems/ItemManager.h"
#include "Inventory/Types/ItemRegistryOptions.h"
#include "Inventory/BP/ItemDataAsset.h"   // FItemData — ItemManager.h 가 include 하지 않아 직접 건다
#include "Inventory/Components/InventoryComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Core/Physics/KineticDamage.h"
#include "NPC/BP/SmartNPC.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"
#include "Engine/World.h"

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

    // PhysicsActor 는 Pawn 도 Block 이라, 물리 바디인 아이템이 다가오는 플레이어 캡슐에
    // 밀려 계속 도망간다. 폰만 통과시키고 바닥·벽(WorldStatic/Dynamic)은 그대로 막아
    // 아이템이 지면에 놓인 채로 남게 한다. 픽업 판정은 콜리전이 아니라 ItemManager
    // 등록 풀 거리 조회라 이 변경에 영향받지 않는다.
    ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    
    // 2. 상호작용 감지용 구체 반경 설정
    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    InteractionSphere->SetupAttachment(RootComponent);
    InteractionSphere->SetSphereRadius(100.f); 
    InteractionSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));

    // 던진 아이템 타격 — 히트 이벤트 자체는 LaunchThrown 이 켠다(SetNotifyRigidBodyCollision).
    // 바인딩은 생성자에서 한 번만 해 두면 되고, 창이 닫힌 뒤엔 ThrownBy 가 비어 있어 무시된다.
    ItemMesh->OnComponentHit.AddDynamic(this, &ADroppedItemBase::OnMeshHit);
}

void ADroppedItemBase::SyncMeshFromItemData()
{
    if (!ItemMesh || ItemData.ItemTemplateID.IsEmpty()) return;

    // ItemManager 는 GameInstance 서브시스템이라 에디터(비 PIE)에서는 없다. 테이블을 직접 연다.
    UDataTable* Table = Cast<UDataTable>(
        StaticLoadObject(UDataTable::StaticClass(), nullptr, ItemRegistryPaths::DefaultItemTable));
    if (!Table) return;

    const FItemData* Row = Table->FindRow<FItemData>(FName(*ItemData.ItemTemplateID), TEXT("SyncMeshFromItemData"));
    if (!Row) return;

    // 메시가 아직 안 붙은 아이템도 있다. 그럴 땐 기존 메시를 그대로 둬서
    // 빈 ID 를 골랐다가 되돌리는 중에 형태가 사라지지 않게 한다.
    if (UStaticMesh* Mesh = Row->WorldMesh.LoadSynchronous())
    {
        ItemMesh->SetStaticMesh(Mesh);
    }
}

#if WITH_EDITOR
void ADroppedItemBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // ItemTemplateID 는 FDroppedItemData 안에 있어 MemberProperty 로 잡힌다.
    const FName Changed = PropertyChangedEvent.GetPropertyName();
    const FName Member = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

    if (Changed == GET_MEMBER_NAME_CHECKED(FDroppedItemData, ItemTemplateID)
        || Member == GET_MEMBER_NAME_CHECKED(ADroppedItemBase, ItemData))
    {
        SyncMeshFromItemData();
    }
}
#endif

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
    if (UItemManager* ItemManager = UItemManager::Get(this))
    {
        ItemManager->RegisterDroppedItem(ItemData.ItemInstanceID, ItemData.ItemActor, ItemData.ItemTemplateID);

        // 질량을 마스터 테이블의 Weight(kg)로 확정한다. 지정하지 않으면 엔진이 충돌 형상의
        // 부피에 기본 밀도를 곱해 추정하는데, 생성 메시는 부피가 제각각이라 돌멩이가 깃털처럼
        // 뜨거나 반대가 된다. 투척 피해가 ½mv² 라 질량이 곧 타격감이기도 하다.
        FItemData Row;
        if (ItemMesh && ItemManager->GetItemDataByID(ItemData.ItemTemplateID, Row) && Row.Weight > 0.f)
        {
            ItemMesh->SetMassOverrideInKg(NAME_None, Row.Weight * FMath::Max(1, Amount), true);
        }
    }
}

void ADroppedItemBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 맵에서 사라지는 어떤 경우(파괴, 레벨 언로드 등)에도 댕글링 포인터를 유발하지 않고 매니저 캐시에서 삭제되게 합니다.
    if (UItemManager* ItemManager = UItemManager::Get(this))
    {
        ItemManager->UnregisterDroppedItem(ItemData.ItemInstanceID);
    }

    Super::EndPlay(EndPlayReason);
}

bool ADroppedItemBase::TryPickupInto(UInventoryComponent* Inventory)
{
    UItemManager* ItemManager = UItemManager::Get(this);
    if (!Inventory || !ItemManager) return false;

    FItemData Data;
    if (!ItemManager->GetItemDataByID(ItemData.ItemTemplateID, Data))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DroppedItem] 픽업 실패 — 아이템 데이터 없음: %s"), *ItemData.ItemTemplateID);
        return false;
    }

    if (!Inventory->AddItem(Data, Amount))
    {
        UE_LOG(LogTemp, Log, TEXT("[DroppedItem] 픽업 실패(공간·무게 부족): %s"), *Data.ItemID);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[DroppedItem] 픽업: %s x%d → %s"), *Data.ItemID, Amount, *GetNameSafe(Inventory->GetOwner()));
    // ConsumeItem 이 Destroy → EndPlay 에서 ItemManager 등록 해제까지 처리.
    ConsumeItem();
    return true;
}

void ADroppedItemBase::SetPhysicsFrozen(bool bFrozen)
{
    if (!ItemMesh) return;

    if (bFrozen)
    {
        // 물리 바디가 남아 있으면 자기 캡슐·바닥을 밀어 손이 튀거나 소유자가 밀려난다.
        ItemMesh->SetSimulatePhysics(false);
        ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    else
    {
        // SetCollisionEnabled 만으로는 잠근 동안 바뀐 채널 응답이 복구되지 않는다 — 프로파일 재지정.
        // Pawn 은 평상시 Overlap: 바닥에 놓인 물건이 다가오는 캡슐에 밀려 도망다니지 않게.
        ItemMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
        ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
        ItemMesh->SetSimulatePhysics(true);
    }

    // 상호작용 구체 — 켠 채로 두면 쥔/잠긴 물건이 ItemManager::GetItemsInRange 오버랩에 계속 걸려
    // 반대 손으로 다시 집거나 남이 주워 가게 된다.
    if (InteractionSphere)
    {
        InteractionSphere->SetCollisionEnabled(bFrozen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
    }
}

void ADroppedItemBase::LaunchThrown(const FVector& Velocity, AActor* Thrower,
                                    float DamageScale, float MaxDamage, float MinSpeedMs)
{
    if (!ItemMesh) return;

    ThrowDamageScale = DamageScale;
    ThrowMaxDamage = MaxDamage;
    ThrowMinSpeedMs = MinSpeedMs;
    ThrownBy = Thrower;

    SetPhysicsFrozen(false);

    // 던지는 동안에는 Pawn 을 막는다. OnComponentHit 은 블로킹 충돌에서만 오므로, 평상시처럼
    // Overlap 으로 두면 던진 물건이 NPC 를 그냥 통과해 타격 콜백이 아예 발생하지 않는다.
    // 창이 닫히면(EndThrowWindow) 다시 통과로 되돌려 바닥에 놓인 물건이 지나가는 폰에
    // 밀려 도망다니지 않게 한다.
    ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    // 물리 바디의 Hit 이벤트는 기본으로 꺼져 있다 — 켜지 않으면 OnComponentHit 이 아예 안 온다.
    ItemMesh->SetNotifyRigidBodyCollision(true);
    ItemMesh->SetPhysicsLinearVelocity(Velocity);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(ThrowWindowTimer, this, &ADroppedItemBase::EndThrowWindow,
                                          ThrowDamageWindow, false);
    }
}

void ADroppedItemBase::EndThrowWindow()
{
    ThrownBy = nullptr;
    if (ItemMesh)
    {
        ItemMesh->SetNotifyRigidBodyCollision(false);

        // 던지기가 끝나면 다시 폰을 통과시킨다 — 바닥에 놓인 물건이 다가오는 캡슐에 밀려
        // 계속 도망가는 것을 막으려고 평상시에는 Overlap 으로 둔다.
        ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    }
}

void ADroppedItemBase::OnMeshHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                                 FVector NormalImpulse, const FHitResult& Hit)
{
    AActor* Thrower = ThrownBy.Get();
    if (!Thrower || OtherActor == Thrower) return;

    ASmartNPC* NPC = Cast<ASmartNPC>(OtherActor);
    if (!NPC || !ItemMesh) return;

    const float MassKg = FMath::Max(ItemMesh->GetMass(), 0.01f);

    // 충격량 J = m·Δv 에서 충돌 속도를 역산한다. 히트 콜백 시점의 GetPhysicsLinearVelocity 는
    // 이미 충돌 반영 후 값이라 실제 접근 속도보다 작게 나온다.
    // ponytail: 반발계수를 1로 가정해 최대 2배까지 과대평가된다. 체감이 세면 KineticDamageScale 로 낮추고,
    //           정밀도가 더 필요해지면 던진 창 동안 틱으로 직전 프레임 속도를 샘플링할 것.
    const float SpeedMs = (NormalImpulse.Size() / MassKg) / 100.f;
    if (SpeedMs < ThrowMinSpeedMs) return;

    const float Damage = KineticDamage::Compute(MassKg, SpeedMs, ThrowDamageScale, ThrowMaxDamage);

    APawn* ThrowerPawn = Cast<APawn>(Thrower);
    AController* InstigatorController = ThrowerPawn ? ThrowerPawn->GetController() : nullptr;

    KineticDamage::ApplyToNPC(NPC, Damage, Hit.ImpactPoint, -Hit.ImpactNormal,
                              InstigatorController, this, &Hit);

    UE_LOG(LogTemp, Log, TEXT("[DroppedItemBase] 투척 명중 %s → %s: %.1f dmg (%.2f kg, %.2f m/s)"),
           *ItemData.ItemTemplateID, *NPC->GetName(), Damage, MassKg, SpeedMs);

    // 한 번 맞히면 창을 닫는다 — 같은 던짐으로 튕기며 여러 번 때리지 않게.
    EndThrowWindow();
}

void ADroppedItemBase::ConsumeItem()
{
    // 플레이어나 NPC가 아이템을 먹고 맵에서 없앨 때를 위한 표준 래퍼 함수입니다. 파괴 시 자동으로 EndPlay가 호출되어 안전이 보장됩니다.
    UE_LOG(LogTemp, Log, TEXT("[DroppedItemBase] %s 아이템이 획득/소모되어 월드에서 파괴됩니다."), *ItemData.ItemTemplateID);
    Destroy();
}
