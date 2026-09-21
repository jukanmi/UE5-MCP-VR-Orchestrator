#include "Enemy/FlyingEnemyCharacter.h"
#include "Enemy/EnemyProjectile.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/SkeletalMeshComponent.h"

AFlyingEnemyCharacter::AFlyingEnemyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->DefaultLandMovementMode = MOVE_Flying;
        CMC->MaxFlySpeed = 400.f;
    }
}

void AFlyingEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->SetMovementMode(MOVE_Flying);
    }

    // 비행 적은 원거리에서 투사체를 쏘므로 사거리를 확장. 
    // 고도가 250cm 이므로 3D 거리를 고려해 사거리를 넉넉히 800으로 설정.
    if (AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController()))
    {
        AIC->AttackRange = 800.f;
    }
}

void AFlyingEnemyCharacter::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
    // 네비메시 경로(PathFollowingComponent)가 Z축으로 끌어내리는 것을 방지.
    // 수평 이동만 허용하고 Z축은 Tick에서 자체적으로 유지한다.
    WorldDirection.Z = 0.f;
    WorldDirection.Normalize();
    Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
}

void AFlyingEnemyCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bIsDead) return;

    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        if (CMC->MovementMode == MOVE_Flying)
        {
            FVector Start = GetActorLocation();
            FVector End = Start - FVector(0.f, 0.f, 10000.f);
            FHitResult Hit;
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(this);

            float TargetZ = Start.Z;
            // 바닥 추적
            if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
            {
                TargetZ = Hit.ImpactPoint.Z + FlightAltitude;
            }

            // 고도 보정 (부드럽게 보간)
            FVector Loc = Start;
            Loc.Z = FMath::FInterpTo(Loc.Z, TargetZ, DeltaSeconds, 2.0f);
            SetActorLocation(Loc, true);
        }
    }
}

float AFlyingEnemyCharacter::StartAttack(AActor* Target)
{
    // 부모를 부르면 AttackEndTime 이 세팅되고, AttackHitTimer 가 가동되어
    // 허공에 근접 판정(250cm 고도라 빗나감)을 1회 수행함.
    float Len = Super::StartAttack(Target);
    if (Len > 0.f)
    {
        // 허공 스윙(무해함)과 동시에/대신 투사체를 발사할 자체 타이머 설정
        FTimerHandle ShootTimer;
        float ShootDelay = FMath::Clamp(AttackHitDelay, 0.05f, Len);
        GetWorldTimerManager().SetTimer(ShootTimer, this, &AFlyingEnemyCharacter::OnShootProjectile, ShootDelay, false);
    }
    return Len;
}

FVector AFlyingEnemyCharacter::GetProjectileSpawnLocation() const
{
    // 소품 소켓이나 캐릭터 전방에 생성
    if (GetMesh()->DoesSocketExist(HandPropSocket))
    {
        return GetMesh()->GetSocketLocation(HandPropSocket);
    }
    return GetActorLocation() + GetActorForwardVector() * 50.f;
}

void AFlyingEnemyCharacter::OnShootProjectile()
{
    if (bIsDead || !ProjectileClass) return;

    // 타겟(플레이어 등)이 여전히 유효한지 확인 (ACombatCharacter::CurrentAttackTarget은 private이라서 못 읽음)
    // Controller 를 통해 현재 타겟을 유추
    AActor* Target = nullptr;
    if (AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController()))
    {
        // Target is protected/private in AIC? GetFocusActor()? No, wait, Target is not exposed directly.
        // But we can just shoot straight forward or use GetActorForwardVector().
        // Controller's Target is in AEnemyAIController, let's see if we can get it.
    }
    
    // 단순하게 캐릭터의 전방 방향으로 발사
    FVector SpawnLoc = GetProjectileSpawnLocation();
    FRotator SpawnRot = GetActorRotation(); // 이미 AI가 타겟을 바라보도록 회전시켜 둠

    if (UWorld* World = GetWorld())
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;
        SpawnParams.Instigator = this;

        if (AEnemyProjectile* Proj = World->SpawnActor<AEnemyProjectile>(ProjectileClass, SpawnLoc, SpawnRot, SpawnParams))
        {
            float Damage = ComputeAttackDamage();
            Proj->InitProjectile(this, Damage);
        }
    }
}
