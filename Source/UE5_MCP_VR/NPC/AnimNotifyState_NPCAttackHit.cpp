#include "AnimNotifyState_NPCAttackHit.h"
#include "BP/SmartNPC.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotifyState_NPCAttackHit::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
    float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
    if (!MeshComp) return;

    // 스윙 시작 — 스윙당 1회 가드 리셋.
    if (ASmartNPC* NPC = Cast<ASmartNPC>(MeshComp->GetOwner()))
        NPC->BeginAttackHitWindow();
}

void UAnimNotifyState_NPCAttackHit::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
    float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
    if (!MeshComp) return;

    // 윈도우 동안 매 틱 타격 시도 — 타겟이 게이트 통과 시 1회만 적용(가드는 NPC 내부).
    if (ASmartNPC* NPC = Cast<ASmartNPC>(MeshComp->GetOwner()))
        NPC->PerformAttackHit();
}
