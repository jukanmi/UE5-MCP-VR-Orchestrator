#include "Core/Utils/PawnDeathUtils.h"
#include "Core/Utils/GameplayTagUtils.h"
#include "Core/Types/PlayerGameplayTags.h"      // TAG_State_Idle / TAG_State_Condition_Dead
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void PawnDeathUtils::SaveCheckpoint(const FPlayerAttributes& Stats,
                                    const FVector& Location, const FRotator& Rotation,
                                    bool& bHasCheckpoint, FVector& OutLocation,
                                    FRotator& OutRotation, float& OutHP,
                                    const TCHAR* LogContext)
{
    bHasCheckpoint = true;
    OutLocation    = Location;
    OutRotation    = Rotation;
    OutHP          = Stats.Resources.Health;

    UE_LOG(LogTemp, Log, TEXT("[%s] 체크포인트 저장 — 위치: %s, HP: %.1f"),
           LogContext, *Location.ToString(), OutHP);
}

void PawnDeathUtils::HandleDeath(ACharacter* Pawn, FGameplayTagContainer& Tags,
                                 float RespawnDelay,
                                 FTimerHandle& RespawnTimer, FTimerDelegate RespawnDelegate,
                                 const TCHAR* LogContext)
{
    // GetWorld() null 방어 — 액터 제거 중/미초기화 시 GetWorldTimerManager 역참조 크래시 방지.
    if (!Pawn || !Pawn->GetWorld()) return;

    GameplayTagUtils::RemoveState(Tags, TAG_State_Idle);
    GameplayTagUtils::AddState(Tags, TAG_State_Condition_Dead);

    // 입력 차단
    if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
    {
        Pawn->DisableInput(PC);
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
    // GetWorld() null 방어 — 유효하지 않은 월드 컨텍스트로 GetActorOfClass 호출 크래시 방지.
    if (!Pawn || !Pawn->GetWorld()) return;

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
