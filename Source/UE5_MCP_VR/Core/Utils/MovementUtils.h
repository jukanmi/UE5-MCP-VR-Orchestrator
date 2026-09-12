#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

/** 마찰·제동을 0 으로 덮기 전 원본 — EndFrictionlessLaunch 가 되돌린다. */
struct FSavedFriction
{
    float GroundFriction = 0.f;
    float BrakingDecelWalking = 0.f;
    float BrakingFrictionFactor = 0.f;
};

/**
 * MovementUtils
 *
 * "마찰·제동 0 → LaunchCharacter → 원복 + 수평 잔류 속도 제거" 등속 돌진.
 * 플레이어 대쉬(AVRPawn)와 NPC 회피(UNPCActionComponent)가 같은 본문을 각자 들고 있던 것을 합쳤다.
 * 마찰을 0 으로 두면 Launch 속도가 감쇠 없이 유지돼 등속이 되고, MaxWalkSpeed 는 입력 가속에만
 * 적용되므로 건드리지 않는다. 원복 시 마찰만 되돌리면 몇 프레임 더 미끄러지므로 수평 속도를
 * 즉시 0 으로 — 낙하 Z 는 유지.
 */
namespace MovementUtils
{
    inline void BeginFrictionlessLaunch(ACharacter& Character, const FVector& LaunchVelocity, FSavedFriction& OutSaved)
    {
        UCharacterMovementComponent* MC = Character.GetCharacterMovement();
        if (!MC) return;

        OutSaved.GroundFriction        = MC->GroundFriction;
        OutSaved.BrakingDecelWalking   = MC->BrakingDecelerationWalking;
        OutSaved.BrakingFrictionFactor = MC->BrakingFrictionFactor;
        MC->GroundFriction             = 0.f;
        MC->BrakingDecelerationWalking = 0.f;
        MC->BrakingFrictionFactor      = 0.f;

        Character.LaunchCharacter(LaunchVelocity, /*bXYOverride=*/true, /*bZOverride=*/false); // Z 미오버라이드 — 중력 유지
    }

    inline void EndFrictionlessLaunch(UCharacterMovementComponent& MC, const FSavedFriction& Saved)
    {
        MC.GroundFriction             = Saved.GroundFriction;
        MC.BrakingDecelerationWalking = Saved.BrakingDecelWalking;
        MC.BrakingFrictionFactor      = Saved.BrakingFrictionFactor;
        MC.Velocity.X = 0.f;
        MC.Velocity.Y = 0.f;
    }
}
