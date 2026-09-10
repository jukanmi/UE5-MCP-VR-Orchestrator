#pragma once

#include "CoreMinimal.h"

class ASmartNPC;
class AActor;
class AController;
struct FHitResult;

/**
 * 동역학 데미지 공용 헬퍼 — 근접 스윙(VRPawn)·투사체(KineticProjectile) 단일 소스.
 * 운동에너지 ½mv² 를 HP 데미지로 환산하고, 부위 인지·래그돌 임펄스를 위해
 * FPointDamageEvent(본 이름·발사 방향 포함)로 SmartNPC 에 전달한다.
 */
namespace KineticDamage
{
    /** ½·m·v² · Scale 를 [0, MaxDamage] 로 클램프. MassKg=kg, SpeedMs=m/s, 반환=HP 데미지. */
    inline float Compute(float MassKg, float SpeedMs, float Scale, float MaxDamage)
    {
        const float Energy = 0.5f * MassKg * SpeedMs * SpeedMs;   // ½mv² (J)
        return FMath::Clamp(Energy * Scale, 0.f, MaxDamage);
    }

    /**
     * FPointDamageEvent 로 NPC 에 데미지 적용. 충돌점 최근접 본을 BoneName 에 채워
     * 부위 배율(SmartNPC)·래그돌 임펄스(ShotDirection)가 동작하게 한다.
     * BaseHit 제공 시 그 HitInfo 를 유지(투사체 충돌 결과), nullptr 이면 ImpactPoint 만 채운다(근접).
     */
    void ApplyToNPC(ASmartNPC* NPC, float Damage, const FVector& ImpactPoint,
        const FVector& ShotDirection, AController* InstigatorController, AActor* Causer,
        const FHitResult* BaseHit = nullptr);
}
