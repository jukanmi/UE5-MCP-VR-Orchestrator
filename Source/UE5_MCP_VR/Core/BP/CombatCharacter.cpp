#include "Core/BP/CombatCharacter.h"
#include "Core/Physics/KineticDamage.h"
#include "Core/Physics/NPCBoneCapsuleSet.h"
#include "Core/Types/CollisionChannels.h"
#include "Core/Types/PlayerGameplayTags.h"
#include "Core/Types/CharacterAttributes.h"
#include "Core/Utils/GameplayTagUtils.h"
#include "NPC/Components/NPCRagdollComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Sound/SoundBase.h"
#include "Components/CapsuleComponent.h"
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

static TAutoConsoleVariable<bool> CVarDrawBoneCapsules(TEXT("npc.DrawBoneCapsules"), false, TEXT("NPC 뼈 캡슐(손 충돌 판정용)을 디버그 드로우로 그린다."));

ACombatCharacter::ACombatCharacter()
{
    RagdollComponent = CreateDefaultSubobject<UNPCRagdollComponent>(TEXT("Ragdoll"));

    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
    StimuliSource->RegisterForSense(UAISense_Hearing::StaticClass());
    StimuliSource->RegisterWithPerceptionSystem();

    // AI 캐릭터 공통 — 기본 Character 는 컨트롤러 Yaw 를 따라 돌고 이동 방향을 향하지 않아 MoveTo 시 옆걸음·뒷걸음이 난다.
    // 컨트롤러 회전은 무시하고 이동 방향으로 자동 선회. 선회 속도(RotationRate)는 파생 클래스가 정한다.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->bOrientRotationToMovement = true;
        CMC->bUseControllerDesiredRotation = false;
    }
}

void ACombatCharacter::AddStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::AddState(GameplayTags, Tag);
}

void ACombatCharacter::RemoveStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::RemoveState(GameplayTags, Tag);
}

bool ACombatCharacter::BeginDeath()
{
    if (bIsDead) return false;
    bIsDead = true;
    GameplayTags.Reset();
    AddStateTag(TAG_State_Condition_Dead);
    return true;
}

float ACombatCharacter::NoteHit(FDamageEvent const& DamageEvent, const AActor* DamageCauser)
{
    if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        const FPointDamageEvent& Pt = static_cast<const FPointDamageEvent&>(DamageEvent);
        if (RagdollComponent) RagdollComponent->NoteHit(Pt.HitInfo.BoneName, Pt.ShotDirection);
        return BodyPartMultiplierForBone(Pt.HitInfo.BoneName);
    }
    if (RagdollComponent)
    {
        RagdollComponent->NoteHit(NAME_None, DamageCauser
            ? (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal()
            : -GetActorForwardVector());
    }
    return 1.f;
}

void ACombatCharacter::PlayOneOf(const TArray<USoundBase*>& Sounds) const
{
    if (Sounds.Num() == 0) return;
    if (USoundBase* S = Sounds[FMath::RandRange(0, Sounds.Num() - 1)])
    {
        UGameplayStatics::PlaySoundAtLocation(this, S, GetActorLocation());
    }
}

void ACombatCharacter::InitAttributes(FCharacterAttributesBase& Attributes)
{
    Attributes.RecalculateCombatStats();
    Attributes.Resources.Health = Attributes.Resources.MaxHealth;
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->MaxWalkSpeed = Attributes.Movement.WalkSpeed;
    }
}

void ACombatCharacter::BeginPlay()
{
    Super::BeginPlay();   // BP BeginPlay 가 메시를 바꿔도 그 뒤에 판단한다

    // 이 메시에 뼈 캡슐 치수가 있으면 손은 뼈 캡슐(VRPawn 이 드라이브 목표를 밀어냄)에만 막히고 몸 캡슐은 손을 통과시킨다.
    // 치수가 없으면 지금처럼 몸 캡슐에 막힌다. 한쪽만 무시해도 충돌 응답은 둘 중 약한 쪽을 따른다.
    if (BoneCapsules && GetMesh() && BoneCapsules->HasFits(GetMesh()->GetSkeletalMeshAsset()))
    {
        GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_HandLeft, ECR_Ignore);
        GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_HandRight, ECR_Ignore);
    }
}

void ACombatCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // 주민·적은 틱 간격이 0.1초라 한 프레임짜리 선은 깜박인다 — 다음 틱까지 남긴다.
    if (BoneCapsules && CVarDrawBoneCapsules.GetValueOnGameThread()) BoneCapsules->DrawDebug(GetMesh(), GetActorTickInterval());
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
