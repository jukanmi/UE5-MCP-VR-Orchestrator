#include "Villager/VillagerCharacter.h"
#include "Villager/VillagerAIController.h"
#include "Core/Utils/GameplayTagUtils.h"
#include "Core/Types/PlayerGameplayTags.h"
#include "NPC/Components/NPCRagdollComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimSequence.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"

AVillagerCharacter::AVillagerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;  // 로코모션 클립 선택만 — 행동은 컨트롤러 틱
    PrimaryActorTick.TickInterval = 0.1f;
    AIControllerClass = AVillagerAIController::StaticClass();
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);  // AnimBP 없이 클립 직접 재생
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    RagdollComponent = CreateDefaultSubobject<UNPCRagdollComponent>(TEXT("Ragdoll"));

    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    if (StimuliSource)
    {
        StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
        StimuliSource->RegisterForSense(UAISense_Hearing::StaticClass());
        StimuliSource->RegisterWithPerceptionSystem();
    }

    // 적·SmartNPC 와 동일 — 컨트롤러 회전 무시 + 이동 방향으로 자동 선회(옆걸음 방지).
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->bOrientRotationToMovement = true;
        CMC->RotationRate = FRotator(0.f, 360.f, 0.f);
        CMC->bUseControllerDesiredRotation = false;
    }
}

void AVillagerCharacter::BeginPlay()
{
    Super::BeginPlay();

    // BaseStats(BP 편집) → 파생치. HP 는 만땅에서 시작.
    Attributes.RecalculateCombatStats();
    Attributes.Resources.Health = Attributes.Resources.MaxHealth;
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->MaxWalkSpeed = Attributes.Movement.WalkSpeed;
    }

    GameplayTagUtils::AddState(GameplayTags, TAG_State_Idle);
    PlayLoco(IdleAnim);
}

void AVillagerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateLocomotionAnim();
}

// ============================================================================
// 애니메이션 (단일 노드)
// ============================================================================
void AVillagerCharacter::PlayLoco(UAnimSequence* Anim)
{
    if (!Anim || CurrentLocoAnim.Get() == Anim) return;
    CurrentLocoAnim = Anim;
    GetMesh()->PlayAnimation(Anim, /*bLooping=*/true);
}

void AVillagerCharacter::UpdateLocomotionAnim()
{
    if (bIsDead || IsPlayingOneShot()) return;
    if (RagdollComponent && RagdollComponent->IsKnockedDown()) return;  // 래그돌이 메시를 쥐고 있음
    const float Speed = GetVelocity().Size2D();
    const float RunThreshold = (Attributes.Movement.WalkSpeed + Attributes.Movement.RunSpeed) * 0.5f;
    PlayLoco(Speed < 10.f ? IdleAnim : (Speed < RunThreshold ? WalkAnim : RunAnim));
}

float AVillagerCharacter::PlayOneShot(UAnimSequence* Anim)
{
    if (!Anim || bIsDead || IsPlayingOneShot()) return 0.f;
    if (RagdollComponent && RagdollComponent->IsKnockedDown()) return 0.f;
    CurrentLocoAnim = nullptr;  // 끝나면 로코모션 틱이 되돌림
    GetMesh()->PlayAnimation(Anim, /*bLooping=*/false);
    const float Len = Anim->GetPlayLength();
    OneShotEndTime = GetWorld()->GetTimeSeconds() + Len;
    return Len;
}

float AVillagerCharacter::PlayWave()
{
    return PlayOneShot(WaveAnim);
}

bool AVillagerCharacter::IsPlayingOneShot() const
{
    return GetWorld() && GetWorld()->GetTimeSeconds() < OneShotEndTime;
}

void AVillagerCharacter::PlayOneOf(const TArray<USoundBase*>& Sounds) const
{
    if (Sounds.Num() == 0) return;
    if (USoundBase* S = Sounds[FMath::RandRange(0, Sounds.Num() - 1)])
    {
        UGameplayStatics::PlaySoundAtLocation(this, S, GetActorLocation());
    }
}

// ============================================================================
// 피격
// ============================================================================
float AVillagerCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (bIsDead) return 0.f;

    const float Raw = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

    // 부위 배율 + 래그돌 임펄스 방향 — 적·SmartNPC 와 같은 규약.
    float Multiplier = 1.0f;
    if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        const FPointDamageEvent& Pt = static_cast<const FPointDamageEvent&>(DamageEvent);
        Multiplier = BodyPartMultiplierForBone(Pt.HitInfo.BoneName);
        if (RagdollComponent) RagdollComponent->NoteHit(Pt.HitInfo.BoneName, Pt.ShotDirection);
    }
    else if (RagdollComponent)
    {
        RagdollComponent->NoteHit(NAME_None, DamageCauser
            ? (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal()
            : -GetActorForwardVector());
    }

    const float Effective = FMath::Max(0.f, Raw - Attributes.Combat.Defense) * Multiplier;
    Attributes.Resources.Health -= Effective;
    UE_LOG(LogTemp, Log, TEXT("[Villager] %s 피격 %.1f (raw %.1f, def %.1f, x%.2f) HP %.0f/%.0f"),
        *VillagerID, Effective, Raw, Attributes.Combat.Defense, Multiplier,
        Attributes.Resources.Health, Attributes.Resources.MaxHealth);

    if (!Attributes.Resources.IsAlive())
    {
        HandleDeath();
        return Effective;
    }

    // 원샷 진행 중(인사)이어도 피격 클립이 덮어쓴다 — 맞았는데 손 흔들면 어색.
    OneShotEndTime = -1.f;
    PlayOneShot(HitAnim);
    if (RagdollComponent) RagdollComponent->ReactToHit(Effective);
    PlayOneOf(HitSounds);

    // 반격 없음 — 때린 쪽(플레이어·적·아군)에게서 도망. 투사체는 Causer 가 탄이라 Instigator 폰을 우선.
    AActor* Attacker = (EventInstigator && EventInstigator->GetPawn()) ? Cast<AActor>(EventInstigator->GetPawn()) : DamageCauser;
    if (AVillagerAIController* AIC = Cast<AVillagerAIController>(GetController()))
    {
        AIC->OnThreat(Attacker);
    }
    return Effective;
}

// ============================================================================
// 사망
// ============================================================================
void AVillagerCharacter::HandleDeath()
{
    if (bIsDead) return;
    bIsDead = true;

    GameplayTags.Reset();
    GameplayTagUtils::AddState(GameplayTags, TAG_State_Condition_Dead);
    OneShotEndTime = -1.f;

    // AI 정지 — UnPossess 하지 않는다: 폰이 Destroy 될 때 PawnPendingDestroy 가 컨트롤러를 같이 지우게(고아 방지).
    if (AVillagerAIController* AIC = Cast<AVillagerAIController>(GetController()))
    {
        AIC->OnPawnDied();
    }

    if (RagdollComponent) RagdollComponent->EnterDeathRagdoll();
    PlayOneOf(DeathSounds);
    SetLifeSpan(CorpseLifetime);

    UE_LOG(LogTemp, Log, TEXT("[Villager] %s 사망 — %.1f초 후 제거"), *VillagerID, CorpseLifetime);
}
