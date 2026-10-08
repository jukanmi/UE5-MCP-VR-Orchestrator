#include "Story/StoryZoneTrigger.h"
#include "Components/BoxComponent.h"
#include "Core/Interfaces/Entity.h"
#include "NPC/Subsystems/NPCManager.h"
#include "POI/POIManager.h"

AStoryZoneTrigger::AStoryZoneTrigger()
{
    PrimaryActorTick.bCanEverTick = false;

    Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
    SetRootComponent(Box);
    Box->SetBoxExtent(FVector(450.f, 450.f, 200.f));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Box->SetGenerateOverlapEvents(true);
    Box->OnComponentBeginOverlap.AddDynamic(this, &AStoryZoneTrigger::HandleBeginOverlap);
}

void AStoryZoneTrigger::HandleBeginOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool,
                                           const FHitResult&)
{
    // NPC 도 폰이라 겹친다 — 플레이어만.
    if (!OtherActor || !OtherActor->Implements<UPlayerBase>()) return;
    if (bOnce && bFired) return;
    if (ZoneName.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[StoryZone] %s: ZoneName 비어 있음 — 송신 생략"), *GetName());
        return;
    }
    // PoiId 는 첫 진입 때 한 번만 확인 — BeginPlay 시점엔 POI 가 아직 등록 전일 수 있다. 실패해도 송신은 그대로.
    if (!bPoiChecked && !PoiId.IsEmpty())
    {
        bPoiChecked = true;
        const UPOIManager* PoiManager = GetWorld() ? GetWorld()->GetSubsystem<UPOIManager>() : nullptr;
        if (!PoiManager || !PoiManager->FindById(PoiId))
        {
            UE_LOG(LogTemp, Warning, TEXT("[StoryZone] %s: PoiId '%s' 에 해당하는 POI 가 등록돼 있지 않음 (ZoneName=%s)"),
                   *GetName(), *PoiId, *ZoneName);
        }
    }
    bFired = true;
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->SendStoryEvent(TEXT("zone_enter"), ZoneName);
    }
}
