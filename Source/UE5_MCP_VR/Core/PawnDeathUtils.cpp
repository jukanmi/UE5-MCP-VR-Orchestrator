#include "PawnDeathUtils.h"
#include "GameplayTagUtils.h"
#include "PlayerGameplayTags.h"      // TAG_State_Idle / TAG_State_Condition_Dead
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void PawnDeathUtils::SaveCheckpoint(const FPlayerAttributes& Stats, bool bRequireAlive,
                                    const FVector& Location, const FRotator& Rotation,
                                    bool& bHasCheckpoint, FVector& OutLocation,
                                    FRotator& OutRotation, float& OutHP,
                                    const TCHAR* LogContext)
{
    if (bRequireAlive && !Stats.Resources.IsAlive()) return;

    bHasCheckpoint = true;
    OutLocation    = Location;
    OutRotation    = Rotation;
    OutHP          = Stats.Resources.Health;

    UE_LOG(LogTemp, Log, TEXT("[%s] 체크포인트 저장 — 위치: %s, HP: %.1f"),
           LogContext, *Location.ToString(), OutHP);
}

void PawnDeathUtils::HandleDeath(ACharacter* Pawn, FGameplayTagContainer& Tags,
                                 bool bClearCursor, float RespawnDelay,
                                 FTimerHandle& RespawnTimer, FTimerDelegate RespawnDelegate,
                                 const TCHAR* LogContext)
{
    if (!Pawn) return;

    GameplayTagUtils::RemoveState(Tags, TAG_State_Idle);
    GameplayTagUtils::AddState(Tags, TAG_State_Condition_Dead);

    // 입력 차단
    if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
    {
        Pawn->DisableInput(PC);
        if (bClearCursor)
        {
            PC->bShowMouseCursor = false;
            PC->SetInputMode(FInputModeGameOnly());
        }
    }

    // 충돌/메시 비활성화
    if (UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent())
        Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (USkeletalMeshComponent* Mesh = Pawn->GetMesh())
        Mesh->SetVisibility(false);

    UE_LOG(LogTemp, Warning, TEXT("[%s] 사망 — %.1f초 후 리스폰"), LogContext, RespawnDelay);

    Pawn->GetWorldTimerManager().SetTimer(RespawnTimer, RespawnDelegate, RespawnDelay, false);
}

void PawnDeathUtils::Respawn(ACharacter* Pawn, FPlayerAttributes& Stats,
                             bool bHasCheckpoint, const FVector& CheckpointLocation,
                             const FRotator& CheckpointRotation, float CheckpointHP,
                             FGameplayTagContainer& Tags, const TCHAR* LogContext)
{
    if (!Pawn) return;

    if (bHasCheckpoint)
    {
        Pawn->SetActorLocationAndRotation(CheckpointLocation, CheckpointRotation);
        Stats.Resources.Health = CheckpointHP;
    }
    else
    {
        // 체크포인트 미도달 시 PlayerStart 폴백
        if (AActor* Start = UGameplayStatics::GetActorOfClass(Pawn->GetWorld(), APlayerStart::StaticClass()))
            Pawn->SetActorLocationAndRotation(Start->GetActorLocation(), Start->GetActorRotation());
        Stats.Resources.Health = Stats.Resources.MaxHealth;
    }

    // 충돌/메시 복구
    if (UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent())
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    if (USkeletalMeshComponent* Mesh = Pawn->GetMesh())
        Mesh->SetVisibility(true);

    // 태그 초기화
    GameplayTagUtils::RemoveState(Tags, TAG_State_Condition_Dead);
    GameplayTagUtils::AddState(Tags, TAG_State_Idle);

    // 입력 복구
    if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
        Pawn->EnableInput(PC);

    UE_LOG(LogTemp, Log, TEXT("[%s] 리스폰 완료 — HP: %.1f"), LogContext, Stats.Resources.Health);
}
