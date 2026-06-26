#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KineticProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * [의도(Why)] §4 동역학 데미지의 원거리 무기. VRPawn 의 IA_Attack 으로 발사되며,
 *  명중 시 자신의 운동에너지(½·m·v²)를 데미지로 환산해 FPointDamageEvent 로 전달한다.
 *  부위 인지(HitInfo.BoneName)·래그돌 임펄스(ShotDirection)는 SmartNPC::TakeDamage 가
 *  근접 스윙과 동일하게 처리 — 데미지 전달 규약을 FPointDamageEvent 로 단일화.
 */
UCLASS()
class UE5_MCP_VR_API AKineticProjectile : public AActor
{
    GENERATED_BODY()

public:
    AKineticProjectile();

    /** 발사 직후 VRPawn 이 호출 — J→HP 환산 계수·상한·발사자 무시 주입. */
    void InitProjectile(AActor* Shooter, float InDamageScale, float InMaxDamage);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    USphereComponent* CollisionComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    UStaticMeshComponent* MeshComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    UProjectileMovementComponent* MoveComp;

    /** 투사체 질량(kg) — ½mv² 의 m. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Kinetic")
    float ProjectileMass = 0.2f;

    /** 초기 속도(cm/s). 비행 중 ½mv² 의 v 는 실시간 MoveComp->Velocity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Kinetic")
    float ProjectileSpeed = 3000.f;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);

private:
    // VRPawn 주입값 — 근접과 동일한 J→HP 환산·상한 사용(전투 일관성).
    float DamageScale = 1.0f;
    float MaxDamage = 100.f;
};
