#pragma once

#include "CoreMinimal.h"
#include "Core/Interfaces/Entity.h"                  // FPlayerAttributes
#include "GameplayTagContainer.h"
#include "TimerManager.h"            // FTimerHandle / FTimerDelegate

class ACharacter;

/**
 * 플레이어 폰 사망/리스폰/체크포인트 공통 헬퍼.
 * (GameplayTags·CurrentStats·Checkpoint 필드는 각 폰이 보유 — 본 헬퍼는 인자로 받아 로직만 공유.)
 * LogContext 는 로그 접두(예: "VRPawn").
 */
namespace PawnDeathUtils
{
    /** 현재 위치/HP 를 체크포인트로 저장. */
    void SaveCheckpoint(const FPlayerAttributes& Stats,
                        const FVector& Location, const FRotator& Rotation,
                        bool& bHasCheckpoint, FVector& OutLocation,
                        FRotator& OutRotation, float& OutHP,
                        const TCHAR* LogContext);

    /** 사망 처리: Dead 태그 세팅·입력차단·충돌off·메시숨김·리스폰 타이머 예약.
     *  RespawnDelegate = 각 폰의 Respawn() 바인딩(FTimerDelegate::CreateUObject). */
    void HandleDeath(ACharacter* Pawn, FGameplayTagContainer& Tags,
                     float RespawnDelay,
                     FTimerHandle& RespawnTimer, FTimerDelegate RespawnDelegate,
                     const TCHAR* LogContext);

    /** 리스폰: 체크포인트/PlayerStart 복귀 + HP 복원 + 충돌·메시 복구 + Idle 태그 + 입력 복구. */
    void Respawn(ACharacter* Pawn, FPlayerAttributes& Stats,
                 bool bHasCheckpoint, const FVector& CheckpointLocation,
                 const FRotator& CheckpointRotation, float CheckpointHP,
                 FGameplayTagContainer& Tags, const TCHAR* LogContext);
}
