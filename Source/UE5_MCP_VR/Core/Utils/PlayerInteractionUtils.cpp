#include "Core/Utils/PlayerInteractionUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "NPC/BP/SmartNPC.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Villager/VillagerCharacter.h"

namespace
{
    // Origin 주변 Radius 구체 안 폰 오버랩(자기 자신 제외). 비어 있으면 월드 없음.
    TArray<FOverlapResult> OverlapPawns(const AActor* Origin, float Radius)
    {
        TArray<FOverlapResult> Overlaps;
        UWorld* World = Origin ? Origin->GetWorld() : nullptr;
        if (!World) return Overlaps;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(Origin);
        World->OverlapMultiByObjectType(
            Overlaps, Origin->GetActorLocation(), FQuat::Identity,
            FCollisionObjectQueryParams(ECollisionChannel::ECC_Pawn),
            FCollisionShape::MakeSphere(Radius), Params);
        return Overlaps;
    }
}

FString PlayerInteractionUtils::FindNearestNPCId(const AActor* Origin, float Radius)
{
    const TArray<FOverlapResult> Overlaps = OverlapPawns(Origin, Radius);

    FString FoundID;
    float MinDistSq = TNumericLimits<float>::Max();
    for (const FOverlapResult& R : Overlaps)
    {
        if (const ASmartNPC* NPC = Cast<ASmartNPC>(R.GetActor()))
        {
            const float D = FVector::DistSquared(Origin->GetActorLocation(), NPC->GetActorLocation());
            if (D < MinDistSq)
            {
                MinDistSq = D;
                FoundID = NPC->AgentID;
            }
        }
    }
    return FoundID;
}

AVillagerCharacter* PlayerInteractionUtils::FindNearestTalkTarget(const AActor* Origin, float Radius, FString& OutNpcId)
{
    OutNpcId.Reset();
    AVillagerCharacter* Villager = nullptr;
    float MinDistSq = TNumericLimits<float>::Max();
    for (const FOverlapResult& R : OverlapPawns(Origin, Radius))
    {
        AActor* A = R.GetActor();
        const ASmartNPC* NPC = Cast<ASmartNPC>(A);
        AVillagerCharacter* V = NPC ? nullptr : Cast<AVillagerCharacter>(A);
        if (!NPC && !V) continue;
        if (V && V->bIsDead) continue;
        const float D = FVector::DistSquared(Origin->GetActorLocation(), A->GetActorLocation());
        if (D >= MinDistSq) continue;
        MinDistSq = D;
        Villager = V;
        OutNpcId = NPC ? NPC->AgentID : FString();
    }
    return Villager;
}

bool PlayerInteractionUtils::SendDialogueToNpc(const UObject* WorldContext, const FString& PlayerId,
    const FString& TargetNpcId, const FString& Text)
{
    if (TargetNpcId.IsEmpty()) return false;
    if (UNPCManager* Manager = UNPCManager::Get(WorldContext))
    {
        Manager->SendPlayerDialogue(PlayerId, TargetNpcId, Text);
        return true;
    }
    return false;
}
