#include "Story/StoryZoneTrigger.h"
#include "Components/BoxComponent.h"
#include "Core/Interfaces/Entity.h"
#include "NPC/Subsystems/NPCManager.h"

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
    bFired = true;
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->SendStoryEvent(TEXT("zone_enter"), ZoneName);
    }
}
