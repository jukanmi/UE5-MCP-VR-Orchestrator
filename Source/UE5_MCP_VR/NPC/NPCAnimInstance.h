#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "NPCAnimInstance.generated.h"

class UNPCStateComponent;

UCLASS()
class UE5_MCP_VR_API UNPCAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
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

    // --- Posture (NPCStateComponent 미러) ---
    // SitDown/LieDown 몽타주는 진입 모션일 뿐 — 블렌드아웃 후 자세를 붙잡는 건 AnimGraph 상태머신.
    // 이 두 플래그가 Locomotion↔Sit_Idle/Lie_Idle 전이 조건. 단일 소스는 NPCStateComponent(§8).

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Posture")
    bool bIsSit = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Posture")
    bool bIsLie = false;

private:
    bool bWasInAir = false;

    /** 소유 NPC 의 상태 컴포넌트 — 매 틱 GetComponentByClass 를 피하려 캐시.
     *  프리뷰 뷰포트의 SkeletalMeshActor 처럼 컴포넌트가 없는 소유자도 있어 약참조 + 유효성 검사. */
    TWeakObjectPtr<UNPCStateComponent> CachedStateComponent;
};
