#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "NPCAnimInstance.generated.h"

UCLASS()
class UE5_MCP_VR_API UNPCAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // 이동 속도 (0=정지, >0=이동). BlendSpace 가로축으로 사용
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
    float Speed = 0.f;

    // 이동 방향 (-180~180도). BlendSpace 세로축(스트레이프)으로 사용
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
    float Direction = 0.f;

    // 공중 여부 (점프/낙하). JumpStart → InAir → JumpEnd 스테이트 전환용
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jump")
    bool bIsInAir = false;

    // 착지 직후 한 프레임 트리거 (JumpLand 애님용)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jump")
    bool bJustLanded = false;

private:
    bool bWasInAir = false;
};
