#include "KineticProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "../NPC/SmartNPC.h"

AKineticProjectile::AKineticProjectile()
{
    PrimaryActorTick.bCanEverTick = false;

    // 충돌 구체가 루트 — 발사자 외 모든 동적 오브젝트(NPC=Pawn, 월드) Block.
    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
    CollisionComp->InitSphereRadius(6.f);
    CollisionComp->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    CollisionComp->SetNotifyRigidBodyCollision(true);
    CollisionComp->OnComponentHit.AddDynamic(this, &AKineticProjectile::OnHit);
    RootComponent = CollisionComp;

    // 시각용 메시 — 충돌 없음(루트 구체가 담당). BP 에서 StaticMesh 지정.
    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComp->SetupAttachment(RootComponent);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // 비행 — 전방으로 발사, 중력 적용, 회전은 속도 방향 추종.
    MoveComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MoveComp"));
    MoveComp->SetUpdatedComponent(CollisionComp);
    MoveComp->InitialSpeed = ProjectileSpeed;
    MoveComp->MaxSpeed = ProjectileSpeed;
    MoveComp->bRotationFollowsVelocity = true;
    MoveComp->bShouldBounce = false;
    MoveComp->ProjectileGravityScale = 1.0f;

    InitialLifeSpan = 5.f;  // 빗나간 투사체 자동 소멸.
}

void AKineticProjectile::BeginPlay()
{
    Super::BeginPlay();
    // 에디터에서 ProjectileSpeed 를 바꿨을 수 있으니 비행 시작 직전 동기화.
    if (MoveComp)
    {
        MoveComp->InitialSpeed = ProjectileSpeed;
        MoveComp->MaxSpeed = ProjectileSpeed;
        MoveComp->Velocity = GetActorForwardVector() * ProjectileSpeed;
    }
}

void AKineticProjectile::InitProjectile(AActor* Shooter, float InDamageScale, float InMaxDamage)
{
    DamageScale = InDamageScale;
    MaxDamage = InMaxDamage;
    if (Shooter && CollisionComp)
    {
        // 발사 순간 자기 손/몸에 닿아 즉시 터지는 것 방지.
        CollisionComp->IgnoreActorWhenMoving(Shooter, true);
    }
}

void AKineticProjectile::OnHit(UPrimitiveComponent* /*HitComp*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
    FVector /*NormalImpulse*/, const FHitResult& Hit)
{
    if (OtherActor && OtherActor != this)
    {
        if (ASmartNPC* NPC = Cast<ASmartNPC>(OtherActor))
        {
            // MoveComp 가 유효할 때만 속도 기반 데미지 산출(생성 실패·지연 소멸 대비).
            if (MoveComp)
            {
                const float SpeedMs = MoveComp->Velocity.Size() / 100.f;   // cm/s → m/s
                const float Energy  = 0.5f * ProjectileMass * SpeedMs * SpeedMs;  // ½mv² (J)
                const float Damage  = FMath::Clamp(Energy * DamageScale, 0.f, MaxDamage);

                // 부위 인지 best-effort — 충돌은 NPC 캡슐과 일어나 Hit.BoneName 이 비어있으므로
                // 충돌점에서 NPC 메시 최근접 본을 찾아 채운다. 실패 시 None→Torso 폴백(SmartNPC).
                FPointDamageEvent Ev;
                Ev.HitInfo = Hit;
                if (USkeletalMeshComponent* NpcMesh = NPC->GetMesh())
                {
                    Ev.HitInfo.BoneName = NpcMesh->FindClosestBone(Hit.ImpactPoint);
                }
                // FPointDamageEvent 로 보내야 SmartNPC 가 BoneName(부위)·ShotDirection(래그돌)을 처리.
                Ev.ShotDirection   = MoveComp->Velocity.GetSafeNormal();
                Ev.DamageTypeClass = UDamageType::StaticClass();
                NPC->TakeDamage(Damage, Ev, GetInstigatorController(), this);
            }
        }
    }
    Destroy();
}
