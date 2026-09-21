#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyCharacter.h"
#include "FlyingEnemyCharacter.generated.h"

class AEnemyProjectile;

/**
 * 비행형 적 (예: 임프).
 * 네비메시 상의 2D 경로를 따라가되, Z축은 바닥에서 FlightAltitude 만큼 띄워서 비행한다.
 * 근접 스윙 대신 원거리 투사체(AEnemyProjectile)를 발사한다.
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API AFlyingEnemyCharacter : public AEnemyCharacter
{
    GENERATED_BODY()

public:
    AFlyingEnemyCharacter();

    /** 비행 고도 (지면으로부터의 높이 cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Flying")
    float FlightAltitude = 250.f;

    /** 비행 투사체 클래스 (마법 구체 등) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Flying")
    TSubclassOf<AEnemyProjectile> ProjectileClass;

    // AEnemyCharacter interface
    virtual float StartAttack(AActor* Target) override;

    // ACharacter interface
    virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) override;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void OnShootProjectile();
    
    // 비행 적은 근접 무기가 없으므로 타격 판정 전용 오프셋/스폰 위치
    FVector GetProjectileSpawnLocation() const;
};
