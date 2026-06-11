#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/AnimMontage.h"
#include "Sound/SoundBase.h"
#include "NPCActionDataAsset.generated.h"

USTRUCT(BlueprintType)
struct FActionMediaData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media")
    UAnimMontage* Montage = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media")
    USoundBase* Sound = nullptr;
    
};

/**
 * 상호작용 키(String)와 재생할 미디어(Montage, Sound)를 매핑하는 데이터 에셋.
 */
UCLASS(BlueprintType)
class UE5_MCP_VR_API UNPCActionDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // Key: InteractionType (e.g., "Sit", "Wave", "Dance_Salsa")
    // Value: 재생할 미디어 데이터
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    TMap<FString, FActionMediaData> ActionMedias;


};
