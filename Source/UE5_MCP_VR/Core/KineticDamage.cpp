#include "KineticDamage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "../NPC/SmartNPC.h"

void KineticDamage::ApplyToNPC(ASmartNPC* NPC, float Damage, const FVector& ImpactPoint,
    const FVector& ShotDirection, AController* InstigatorController, AActor* Causer,
    const FHitResult* BaseHit)
{
    if (!NPC) return;

    // FPointDamageEvent 로 보내야 SmartNPC 가 BoneName(부위 배율)·ShotDirection(래그돌 임펄스)을 처리.
    FPointDamageEvent Ev;
    if (BaseHit)
    {
        Ev.HitInfo = *BaseHit;                 // 투사체 충돌 결과 유지(ImpactPoint·Normal 등)
    }
    else
    {
        Ev.HitInfo.ImpactPoint = ImpactPoint;  // 근접 — 손 위치만
    }

    // 충돌은 NPC 캡슐과 일어나 Hit.BoneName 이 비어있으므로 충돌점 최근접 본으로 채움.
    // 실패 시 None→Torso 폴백(SmartNPC::BoneToBodyPart).
    if (USkeletalMeshComponent* NpcMesh = NPC->GetMesh())
    {
        Ev.HitInfo.BoneName = NpcMesh->FindClosestBone(ImpactPoint);
    }
    Ev.ShotDirection   = ShotDirection;
    Ev.DamageTypeClass = UDamageType::StaticClass();

    NPC->TakeDamage(Damage, Ev, InstigatorController, Causer);
}
