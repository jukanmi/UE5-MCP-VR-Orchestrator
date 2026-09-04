#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_NPCAttackHit.generated.h"

/**
 * NPC 공격 몽타주(AM_Attack)의 스윙 임팩트 구간에 배치하는 히트윈도우 노티파이.
 *
 * 윈도우 동안 소유 ASmartNPC 의 PerformAttackHit 을 매 틱 호출 → 타겟이 거리·arc 게이트를
 * 통과하면 1회 데미지(스윙당 1회 가드는 NPC 가 보유). 판정 로직 자체는 ASmartNPC 에 둔다
 * (공격 스탯·타겟 보유처라 응집도↑). 본 클래스는 윈도우 라이프사이클 위임만 담당.
 */
UCLASS(meta = (DisplayName = "NPC Attack Hit Window"))
class UE5_MCP_VR_API UAnimNotifyState_NPCAttackHit : public UAnimNotifyState
{
    GENERATED_BODY()

public:
    virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                             float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

    virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                            float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
};
