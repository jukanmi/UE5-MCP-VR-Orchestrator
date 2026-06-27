#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * VRPawn·VRPlayerCharacter 공통 플레이어-NPC 상호작용 헬퍼.
 * 두 폰에 복붙돼 있던 최근접 NPC 탐지·플레이어 발화 전송 로직 단일화.
 */
namespace PlayerInteractionUtils
{
    /** Origin 주변 Radius(cm) 내 최근접 SmartNPC 의 AgentID. 없으면 빈 문자열. */
    FString FindNearestNPCId(const AActor* Origin, float Radius);

    /** WorldContext 의 월드에서 UNPCManager 로 플레이어 발화 전송. Manager/대상 없으면 무시(false). */
    bool SendDialogueToNpc(const UObject* WorldContext, const FString& PlayerId,
        const FString& TargetNpcId, const FString& Text);
}
