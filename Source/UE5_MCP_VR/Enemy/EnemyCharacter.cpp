#include "Enemy/EnemyCharacter.h"
#include "Enemy/EnemyAIController.h"
#include "Core/Utils/GameplayTagUtils.h"
#include "Core/Types/PlayerGameplayTags.h"
#include "NPC/Components/NPCRagdollComponent.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimSequence.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "TimerManager.h"

AEnemyCharacter::AEnemyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;  // 로코모션 클립 선택만 — 행동은 컨트롤러 틱
    PrimaryActorTick.TickInterval = 0.1f;
    AIControllerClass = AEnemyAIController::StaticClass();
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);  // AnimBP 없이 클립 직접 재생
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    RagdollComponent = CreateDefaultSubobject<UNPCRagdollComponent>(TEXT("Ragdoll"));

    // 손 소품 — 생성자에선 소켓을 못 정한다(BP 가 HandPropSocket 을 나중에 덮어씀). PostInitializeComponents 에서 붙인다.
    HandProp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandProp"));
    HandProp->SetupAttachment(GetMesh());
    HandProp->SetCollisionEnabled(ECollisionEnabled::NoCollision);  // 캡슐·NPC 인지 방해 금지(타격은 공격 판정이 담당)

    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    if (StimuliSource)
    {
        StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
        StimuliSource->RegisterForSense(UAISense_Hearing::StaticClass());
        StimuliSource->RegisterWithPerceptionSystem();
    }

    // SmartNPC 와 동일 — 컨트롤러 회전 무시 + 이동 방향으로 자동 선회(옆걸음 방지).
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->bOrientRotationToMovement = true;
        CMC->RotationRate = FRotator(0.f, 540.f, 0.f);
        CMC->bUseControllerDesiredRotation = false;
        // RVO 회피는 켜지 않는다 — 켜면 타겟 앞 1.7~2.2m 에서 두 명이 서로 피하느라 v=0 으로 얼어 공격 사거리(170)에
        // 영영 못 든다(실측 32초 무타격). 끼임의 실제 원인은 나무 수관 충돌이었고 그쪽을 고쳤다.
    }
}

void AEnemyCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // BP 상대 트랜스폼(손안 오프셋)은 유지한 채 소켓만 바꿔 단다. 소켓이 없으면 메시 루트에 남아 눈에 띄게 어긋난다(로그로 잡음).
    if (HandProp && HandProp->GetStaticMesh())
    {
        if (GetMesh()->DoesSocketExist(HandPropSocket))
        {
            HandProp->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, HandPropSocket);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[Enemy] %s: HandPropSocket %s 없음 — 소품이 메시 루트에 붙음"), *EnemyID, *HandPropSocket.ToString());
        }
    }
}

void AEnemyCharacter::BeginPlay()
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

void AEnemyCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateLocomotionAnim();
}

// ============================================================================
// 애니메이션 (단일 노드)
// ============================================================================
void AEnemyCharacter::PlayLoco(UAnimSequence* Anim)
{
    if (!Anim || CurrentLocoAnim.Get() == Anim) return;
    CurrentLocoAnim = Anim;
    GetMesh()->PlayAnimation(Anim, /*bLooping=*/true);
}

void AEnemyCharacter::UpdateLocomotionAnim()
{
    if (bIsDead || IsAttacking()) return;
    if (RagdollComponent && RagdollComponent->IsKnockedDown()) return;  // 래그돌이 메시를 쥐고 있음
    const float Speed = GetVelocity().Size2D();
    const float RunThreshold = (Attributes.Movement.WalkSpeed + Attributes.Movement.RunSpeed) * 0.5f;
    PlayLoco(Speed < 10.f ? IdleAnim : (Speed < RunThreshold ? WalkAnim : RunAnim));
}

void AEnemyCharacter::PlayOneOf(const TArray<USoundBase*>& Sounds) const
{
    if (Sounds.Num() == 0) return;
    if (USoundBase* S = Sounds[FMath::RandRange(0, Sounds.Num() - 1)])
    {
        UGameplayStatics::PlaySoundAtLocation(this, S, GetActorLocation());
    }
}

bool AEnemyCharacter::IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const
{
    const UObject* Obj = Other.GetObject();
    return Obj && Obj != this && !Obj->IsA<AEnemyCharacter>();
}

// ============================================================================
// 피격
// ============================================================================
float AEnemyCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (bIsDead) return 0.f;

    const float Raw = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

    // 부위 배율 + 래그돌 임펄스 방향 — SmartNPC::TakeDamage 와 같은 규약.
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

    // NPCStateComponent::ApplyDamage 와 같은 공식: (원시 − 방어력) × 부위 배율.
    const float Effective = FMath::Max(0.f, Raw - Attributes.Combat.Defense) * Multiplier;
    Attributes.Resources.Health -= Effective;
    UE_LOG(LogTemp, Log, TEXT("[Enemy] %s 피격 %.1f (raw %.1f, def %.1f, x%.2f) HP %.0f/%.0f"),
        *EnemyID, Effective, Raw, Attributes.Combat.Defense, Multiplier,
        Attributes.Resources.Health, Attributes.Resources.MaxHealth);

    if (!Attributes.Resources.IsAlive())
    {
        HandleDeath();
        return Effective;
    }

    if (RagdollComponent) RagdollComponent->ReactToHit(Effective);
    PlayOneOf(HitSounds);

    // 반격 — 때린 쪽(플레이어·아군 NPC)을 즉시 타겟으로. 투사체는 Causer 가 탄이라 Instigator 폰을 우선.
    AActor* Attacker = (EventInstigator && EventInstigator->GetPawn()) ? Cast<AActor>(EventInstigator->GetPawn()) : DamageCauser;
    if (AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController()))
    {
        AIC->SetTarget(Attacker);
    }
    return Effective;
}

// ============================================================================
// 공격
// ============================================================================
float AEnemyCharacter::StartAttack(AActor* Target)
{
    if (bIsDead || !Target || IsAttacking()) return 0.f;
    if (RagdollComponent && RagdollComponent->IsKnockedDown()) return 0.f;  // 넘어진 동안 공격 불가

    SetCurrentAttackTarget(Target);

    // 클립 1회 재생(없으면 포즈 유지) — 점유 시간은 클립 길이, 판정은 AttackHitDelay 시점 1회.
    float Len = 0.6f;
    if (AttackAnim)
    {
        CurrentLocoAnim = nullptr;  // 끝나면 로코모션 틱이 Idle 로 되돌림
        GetMesh()->PlayAnimation(AttackAnim, /*bLooping=*/false);
        Len = AttackAnim->GetPlayLength();
    }
    const float Now = GetWorld()->GetTimeSeconds();
    AttackEndTime = Now + Len;
    PlayOneOf(AttackSounds);
    BeginAttackHitWindow();
    GetWorldTimerManager().SetTimer(AttackHitTimer, this, &AEnemyCharacter::OnAttackHitTime,
        FMath::Clamp(AttackHitDelay, 0.05f, Len), false);
    return Len;
}

bool AEnemyCharacter::IsAttacking() const
{
    return GetWorld() && GetWorld()->GetTimeSeconds() < AttackEndTime;
}

void AEnemyCharacter::OnAttackHitTime()
{
    PerformAttackHit();
}

// ============================================================================
// 사망
// ============================================================================
void AEnemyCharacter::HandleDeath()
{
    if (bIsDead) return;
    bIsDead = true;

    GameplayTags.Reset();
    GameplayTagUtils::AddState(GameplayTags, TAG_State_Condition_Dead);
    GetWorldTimerManager().ClearTimer(AttackHitTimer);
    AttackEndTime = -1.f;

    // 스토리 — boss_killed 판정(name=EnemyID). 수량 퀘스트 flag 는 스포너가 OnEnemyDied 에서 센다.
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->SendStoryEvent(TEXT("npc_died"), EnemyID, EnemyID);
    }

    // AI 정지 — UnPossess 하지 않는다: 폰이 Destroy 될 때 PawnPendingDestroy 가 컨트롤러를 같이 지우게(고아 방지).
    if (AEnemyAIController* AIC = Cast<AEnemyAIController>(GetController()))
    {
        AIC->OnPawnDied();
    }

    if (RagdollComponent) RagdollComponent->EnterDeathRagdoll();
    PlayOneOf(DeathSounds);

    OnEnemyDied.Broadcast(this);
    SetLifeSpan(CorpseLifetime);

    UE_LOG(LogTemp, Log, TEXT("[Enemy] %s 사망 — %.1f초 후 제거"), *EnemyID, CorpseLifetime);
}
