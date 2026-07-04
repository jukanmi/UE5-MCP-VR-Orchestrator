#pragma once

#include "CoreMinimal.h"
#include "Entity.h"                  // FPlayerAttributes
#include "GameplayTagContainer.h"
#include "TimerManager.h"            // FTimerHandle / FTimerDelegate

class ACharacter;

/**
 * VRPawn·VRPlayerCharacter 공통 사망/리스폰/체크포인트 헬퍼.
 * 두 폰에 글자 단위로 복붙돼 있던 HandleDeath/Respawn/SaveCheckpoint 본문 단일화.
 * (GameplayTags·CurrentStats·Checkpoint 필드는 각 폰이 보유 — 본 헬퍼는 인자로 받아 로직만 공유.)
 *
 * 폰별 차이는 플래그/인자로 흡수:
 *  - bRequireAlive : 사망 시 체크포인트 저장 스킵 여부(플레이어 폰만 true)
 *  - bClearCursor  : 사망 시 마우스 커서·InputMode 정리 여부(플랫스크린 폰만 true)
 *  - LogContext    : 로그 접두( "VRPawn" / "VRPlayerCharacter" )
 */
namespace PawnDeathUtils
{
    /** 현재 위치/HP 를 체크포인트로 저장. bRequireAlive=true 면 사망 상태에선 저장 스킵. */
    void SaveCheckpoint(const FPlayerAttributes& Stats, bool bRequireAlive,
                        const FVector& Location, const FRotator& Rotation,
                        bool& bHasCheckpoint, FVector& OutLocation,
                        FRotator& OutRotation, float& OutHP,
                        const TCHAR* LogContext);

    /** 사망 처리: Dead 태그 세팅·입력차단·충돌off·메시숨김·리스폰 타이머 예약.
     *  RespawnDelegate = 각 폰의 Respawn() 바인딩(FTimerDelegate::CreateUObject). */
    void HandleDeath(ACharacter* Pawn, FGameplayTagContainer& Tags,
                     bool bClearCursor, float RespawnDelay,
                     FTimerHandle& RespawnTimer, FTimerDelegate RespawnDelegate,
                     const TCHAR* LogContext);

    /** 리스폰: 체크포인트/PlayerStart 복귀 + HP 복원 + 충돌·메시 복구 + Idle 태그 + 입력 복구. */
    void Respawn(ACharacter* Pawn, FPlayerAttributes& Stats,
                 bool bHasCheckpoint, const FVector& CheckpointLocation,
                 const FRotator& CheckpointRotation, float CheckpointHP,
                 FGameplayTagContainer& Tags, const TCHAR* LogContext);
}
