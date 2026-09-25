#include "Enemy/EnemyProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "NPC/Action/NPCActionComponent.h"

AEnemyProjectile::AEnemyProjectile()
{
    PrimaryActorTick.bCanEverTick = false;

    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
    CollisionComp->InitSphereRadius(12.f);
    CollisionComp->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    CollisionComp->SetNotifyRigidBodyCollision(true);
    CollisionComp->OnComponentHit.AddDynamic(this, &AEnemyProjectile::OnHit);
    RootComponent = CollisionComp;

    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComp->SetupAttachment(RootComponent);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    MoveComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MoveComp"));
    MoveComp->SetUpdatedComponent(CollisionComp);
    MoveComp->InitialSpeed = ProjectileSpeed;
    MoveComp->MaxSpeed = ProjectileSpeed;
    MoveComp->bRotationFollowsVelocity = true;
    MoveComp->bShouldBounce = false;
    MoveComp->ProjectileGravityScale = 0.0f; // 마법 투사체는 중력 없이 직진

    InitialLifeSpan = 5.f;
}

void AEnemyProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (MoveComp)
    {
        MoveComp->InitialSpeed = ProjectileSpeed;
        MoveComp->MaxSpeed = ProjectileSpeed;
        MoveComp->Velocity = GetActorForwardVector() * ProjectileSpeed;
    }
    GetWorldTimerManager().SetTimer(WarnTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
    {
        UNPCActionComponent::WarnIncomingProjectile(this, MoveComp ? MoveComp->Velocity : FVector::ZeroVector,
                                                    Shooter.Get(), WarnedNPCs);
    }), 0.1f, true, 0.f);
}

void AEnemyProjectile::InitProjectile(AActor* InShooter, float InDamage)
{
    Shooter = InShooter;
    Damage = InDamage;
    if (InShooter && CollisionComp)
    {
        CollisionComp->IgnoreActorWhenMoving(InShooter, true);
    }
}

void AEnemyProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
    FVector NormalImpulse, const FHitResult& Hit)
{
    if (IsActorBeingDestroyed()) return;

    if (OtherActor && OtherActor != this && OtherActor != Shooter.Get())
    {
        // 타격 방향 추출
        FVector Dir = MoveComp ? MoveComp->Velocity.GetSafeNormal() : GetActorForwardVector();
        
        // 투사체 데미지 적용
        FPointDamageEvent DamageEvent(Damage, Hit, Dir, nullptr);
        AController* InstigatorCtrl = Shooter.IsValid() ? Shooter->GetInstigatorController() : nullptr;
        OtherActor->TakeDamage(Damage, DamageEvent, InstigatorCtrl, Shooter.Get());
    }
    
    Destroy();
}
