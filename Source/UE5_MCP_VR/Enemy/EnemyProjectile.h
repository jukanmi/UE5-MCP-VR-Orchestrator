#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * 필드 적(임프 등)이 발사하는 단순 원거리 마법(투사체).
 * 명중 시 플레이어(또는 지정 타겟)에게 데미지를 주고 소멸한다.
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API AEnemyProjectile : public AActor
{
    GENERATED_BODY()

public:
    AEnemyProjectile();

    void InitProjectile(AActor* InShooter, float InDamage);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    USphereComponent* CollisionComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    UStaticMeshComponent* MeshComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    UProjectileMovementComponent* MoveComp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
    float ProjectileSpeed = 1000.f;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);

private:
    float Damage = 10.f;
    TWeakObjectPtr<AActor> Shooter;

    // 진행 방향 NPC 에게 회피 반사 통지(0.1s 간격, NPC 당 1회) — UNPCActionComponent::WarnIncomingProjectile.
    FTimerHandle WarnTimer;
    TSet<const AActor*> WarnedNPCs;
};
