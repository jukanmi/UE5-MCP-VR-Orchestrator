#pragma once

#include "CoreMinimal.h"

class AActor;
class AVillagerCharacter;

/**
 * VRPawn·VRPlayerCharacter 공통 플레이어-NPC 상호작용 헬퍼.
 * 두 폰에 복붙돼 있던 최근접 NPC 탐지·플레이어 발화 전송 로직 단일화.
 */
namespace PlayerInteractionUtils
{
    /** Origin 주변 Radius(cm) 내 최근접 SmartNPC 의 AgentID. 없으면 빈 문자열. */
    FString FindNearestNPCId(const AActor* Origin, float Radius);

    /** Radius 내 최근접 대화 상대. 주민(AVillagerCharacter)이 SmartNPC 보다 가까우면 주민을 반환(OutNpcId 는 비움),
     *  SmartNPC 가 더 가깝거나 주민이 없으면 nullptr + OutNpcId=AgentID. 둘 다 없으면 nullptr + 빈 문자열.
     *  주민은 NPCManager 미등록이라 서버로 보내면 안 된다 — 호출자가 로컬 응답으로 분기. 사망 주민은 제외. */
    AVillagerCharacter* FindNearestTalkTarget(const AActor* Origin, float Radius, FString& OutNpcId);

    /** WorldContext 의 월드에서 UNPCManager 로 플레이어 발화 전송. Manager/대상 없으면 무시(false). */
    bool SendDialogueToNpc(const UObject* WorldContext, const FString& PlayerId,
        const FString& TargetNpcId, const FString& Text);
}
