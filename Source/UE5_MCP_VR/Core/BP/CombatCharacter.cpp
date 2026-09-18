#include "Core/BP/CombatCharacter.h"
#include "Core/Physics/KineticDamage.h"
#include "Core/Types/PlayerGameplayTags.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "GameplayTagAssetInterface.h"

// 본 이름 → 부위. Mixamo X_Bot(RightArm/RightUpLeg/Hips…)·UE Mannequin(upperarm_r/thigh_r/pelvis…) 양 네이밍 수용.
// 좌우: Mixamo 는 Right/Left 접두, Mannequin 은 _r/_l 접미. 미식별 시 Left 폴백.
static EBodyPartType BoneToBodyPart(FName Bone)
{
    const FString B = Bone.ToString().ToLower();
    if (B.IsEmpty()) return EBodyPartType::Torso;
    // 머리·목 (Head/Neck/HeadTop_End)
    if (B.Contains(TEXT("head")) || B.Contains(TEXT("neck"))) return EBodyPartType::Head;
    // 몸통 — 척추·골반·쇄골/어깨 (Mannequin: spine/pelvis/clavicle, Mixamo: spine/hips/shoulder)
    if (B.Contains(TEXT("spine")) || B.Contains(TEXT("pelvis")) || B.Contains(TEXT("hip"))
        || B.Contains(TEXT("clavicle")) || B.Contains(TEXT("shoulder")))
        return EBodyPartType::Torso;
    const bool bRight = B.Contains(TEXT("right")) || B.EndsWith(TEXT("_r"));
    // 팔·손 (Mannequin: upperarm/lowerarm/hand, Mixamo: arm/forearm/hand)
    if (B.Contains(TEXT("arm")) || B.Contains(TEXT("hand")))
        return bRight ? EBodyPartType::ArmRight : EBodyPartType::ArmLeft;
    // 다리·발 (Mannequin: thigh/calf/foot/ball, Mixamo: upleg/leg/foot/toe)
    if (B.Contains(TEXT("leg")) || B.Contains(TEXT("thigh")) || B.Contains(TEXT("calf"))
        || B.Contains(TEXT("foot")) || B.Contains(TEXT("ball")) || B.Contains(TEXT("toe")))
        return bRight ? EBodyPartType::LegRight : EBodyPartType::LegLeft;
    return EBodyPartType::Torso;
}

float ACombatCharacter::BodyPartMultiplierForBone(FName Bone)
{
    switch (BoneToBodyPart(Bone))
    {
        case EBodyPartType::Head:  return 2.0f;
        case EBodyPartType::Torso: return 1.0f;
        default:                   return 0.75f;  // ArmLeft/ArmRight/LegLeft/LegRight
    }
}

bool ACombatCharacter::IsActorDead(const AActor* Actor)
{
    if (!Actor) return false;

    // 전투 캐릭터 — 사망 처리가 세우는 플래그. Destroy 지연 동안에도 즉시 사망 판정.
    if (const ACombatCharacter* C = Cast<ACombatCharacter>(Actor))
    {
        return C->bIsDead;
    }

    // 플레이어(VRPawn) — PawnDeathUtils::HandleDeath 가 부여하는 사망 태그.
    if (const IGameplayTagAssetInterface* TagOwner = Cast<IGameplayTagAssetInterface>(Actor))
    {
        return TagOwner->HasMatchingGameplayTag(TAG_State_Condition_Dead);
    }

    return false;
}

void ACombatCharacter::PerformAttackHit()
{
    if (bAttackHitConsumed || bIsDead) return;

    AActor* Target = CurrentAttackTarget.Get();
    // 자가 공격 방지 — 타겟팅 오작동으로 this 지정 시 자해 버그 차단.
    if (!Target || Target == this) return;

    // 거리 게이트 — 아직 안 닿았으면 다음 틱 재시도(윈도우 동안 타겟이 들어올 수 있음).
    const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
    const float Dist = ToTarget.Size();
    if (Dist > AttackHitRange) return;

    // 정면 arc 게이트 — 등 뒤·옆 타겟 무시.
    const FVector Dir = ToTarget.GetSafeNormal();
    if (FVector::DotProduct(GetActorForwardVector(), Dir) < AttackHitArcCos) return;

    const float Damage = FMath::Max(0.f, ComputeAttackDamage());
    const FVector Impact = Target->GetActorLocation();

    if (ACombatCharacter* TargetChar = Cast<ACombatCharacter>(Target))
    {
        if (TargetChar->bIsDead) return;  // 이미 죽은 대상 재타격 방지 — 가드 소비 안 함
        // 부위 배율·래그돌·(SmartNPC 면) LLM 인지까지 일괄(플레이어→NPC 와 동일 규약).
        KineticDamage::ApplyToNPC(TargetChar, Damage, Impact, Dir, GetController(), this);
    }
    else
    {
        // 플레이어 — 단순 HP 감소. 방향 정보용 FPointDamageEvent.
        FPointDamageEvent Ev;
        Ev.Damage              = Damage;
        Ev.ShotDirection       = Dir;
        Ev.HitInfo.ImpactPoint = Impact;
        Ev.HitInfo.Location    = Impact;
        Target->TakeDamage(Damage, Ev, GetController(), this);

        // 넉백.
        if (ACharacter* TargetChar2 = Cast<ACharacter>(Target))
        {
            if (NPCKnockbackSpeed > 0.f)
                TargetChar2->LaunchCharacter(Dir * NPCKnockbackSpeed, /*bXYOverride=*/true, /*bZOverride=*/false);
        }
    }

    bAttackHitConsumed = true;

    if (bDebugAttackHit && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red,
            FString::Printf(TEXT("[Attack] %s → %s dmg=%.1f dist=%.0f"),
                *GetName(), *Target->GetName(), Damage, Dist));
    }
}
