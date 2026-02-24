#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/AnimMontage.h"
#include "NPCInteractionDataAsset.generated.h"

/**
 * 상호작용 키(String)와 재생할 몽타주(AnimMontage)를 매핑하는 데이터 에셋.
 */
UCLASS(BlueprintType)
class UE5_MCP_VR_API UNPCInteractionDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // Key: InteractionType (e.g., "Sit", "Wave", "Dance_Salsa")
    // Value: 재생할 몽타주
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    TMap<FString, UAnimMontage*> InteractionMontages;

    // 몽타주 검색 유틸리티
    UAnimMontage* FindMontage(const FString& InteractionKey) const
    {
        if (const UAnimMontage* const* Found = InteractionMontages.Find(InteractionKey))
        {
            // Cast const away because PlayAnimMontage takes non-const pointer, 
            // but effectively treats it as read-only.
            return const_cast<UAnimMontage*>(*Found);
        }
        return nullptr;
    }
};
