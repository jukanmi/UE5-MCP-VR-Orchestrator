#include "PlayerInteractionUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "../NPC/BP/SmartNPC.h"
#include "../NPC/NPCManager.h"

FString PlayerInteractionUtils::FindNearestNPCId(const AActor* Origin, float Radius)
{
    if (!Origin) return FString();
    UWorld* World = Origin->GetWorld();
    if (!World) return FString();

    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Origin);
    World->OverlapMultiByObjectType(
        Overlaps, Origin->GetActorLocation(), FQuat::Identity,
        FCollisionObjectQueryParams(ECollisionChannel::ECC_Pawn),
        FCollisionShape::MakeSphere(Radius), Params);

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
