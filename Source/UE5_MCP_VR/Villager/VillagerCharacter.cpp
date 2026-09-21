#include "Villager/VillagerCharacter.h"
#include "Villager/VillagerAIController.h"
#include "Core/Utils/GameplayTagUtils.h"
#include "Core/Types/PlayerGameplayTags.h"
#include "NPC/Components/NPCRagdollComponent.h"
#include "NPC/Components/NPCDialogueUIComponent.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Story/StorySubsystem.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimSequence.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
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

    // 말풍선 — 위젯 공간·크기·머리 위 오프셋은 컴포넌트 기본값(SmartNPC 와 동일).
    DialogueWidgetComp = CreateDefaultSubobject<UNPCDialogueUIComponent>(TEXT("DialogueWidget"));
    DialogueWidgetComp->SetupAttachment(GetMesh());

    // 서브퀘스트 "!" — 말풍선(z210)보다 위, 항상 켠 채 가시성만 토글(빌보드는 UpdateQuestMarker 가 보일 때만 갱신).
    QuestMarkerText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("QuestMarker"));
    QuestMarkerText->SetupAttachment(GetMesh());
    QuestMarkerText->SetRelativeLocation(FVector(0.f, 0.f, 240.f));
    QuestMarkerText->SetText(FText::FromString(TEXT("!")));
    QuestMarkerText->SetTextRenderColor(FColor::Yellow);
    QuestMarkerText->SetHorizontalAlignment(EHTA_Center);
    QuestMarkerText->SetVerticalAlignment(EVRTA_TextCenter);
    QuestMarkerText->SetWorldSize(45.f);
    QuestMarkerText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    QuestMarkerText->SetVisibility(false);

    Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));

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
    UpdateQuestMarker();
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
// 대화 (로컬 규칙 — 서버·LLM 0)
// ============================================================================
const FString& AVillagerCharacter::PickLine(const TArray<FString>& Pool)
{
    static const FString Empty;
    return Pool.Num() ? Pool[FMath::RandRange(0, Pool.Num() - 1)] : Empty;
}

bool AVillagerCharacter::ContainsAny(const FString& Text, const TArray<FString>& Keywords)
{
    for (const FString& K : Keywords)
    {
        if (!K.IsEmpty() && Text.Contains(K)) return true;
    }
    return false;
}

void AVillagerCharacter::Say(const FString& Text)
{
    if (Text.IsEmpty() || bIsDead || !DialogueWidgetComp) return;
    DialogueWidgetComp->ShowSubtitle(Text);
    UE_LOG(LogTemp, Log, TEXT("[Villager] %s: %s"), *VillagerID, *Text);
}

bool AVillagerCharacter::Interact(AActor* Player)
{
    if (bIsDead) return false;
    AVillagerAIController* AIC = Cast<AVillagerAIController>(GetController());
    if (!AIC || !AIC->GreetNow(Player, /*bWave=*/true)) return false;  // 도주·복귀 중엔 인사 안 함

    const FString OfferedSide = FindOfferedSideId();
    if (!OfferedSide.IsEmpty())
    {
        Say(QuestOffers.FindRef(OfferedSide));
        if (UNPCManager* Mgr = UNPCManager::Get(this))
        {
            Mgr->SendStoryEvent(TEXT("quest_accept"), OfferedSide);  // available → active (서버가 멱등 판정)
        }
        return true;
    }

    Say(PickLine(DefaultLines));
    return true;
}

FString AVillagerCharacter::FindOfferedSideId() const
{
    const UStorySubsystem* Story = UStorySubsystem::Get(this);
    if (!Story || !Story->HasState()) return FString();
    const FStoryState& St = Story->GetCurrentState();
    for (const TPair<FString, FString>& Offer : QuestOffers)
    {
        if (St.AvailableSide.Contains(Offer.Key)) return Offer.Key;
    }
    return FString();
}

void AVillagerCharacter::UpdateQuestMarker()
{
    if (!QuestMarkerText) return;
    const bool bHasOffer = !FindOfferedSideId().IsEmpty();
    QuestMarkerText->SetVisibility(bHasOffer);
    if (!bHasOffer) return;

    // 말풍선과 같은 빌보드 공식 — Yaw 만 카메라로, 텍스트는 직립 유지.
    if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        const FVector ToCam = Cam->GetCameraLocation() - QuestMarkerText->GetComponentLocation();
        QuestMarkerText->SetWorldRotation(FRotator(0.f, ToCam.Rotation().Yaw + 180.f, 0.f));
    }
}

FString AVillagerCharacter::BuildDirections() const
{
    const UStorySubsystem* Story = UStorySubsystem::Get(this);
    if (!Story || !Story->HasState()) return NoStoryDirection;

    const FStoryState& St = Story->GetCurrentState();
    FString Out = BeatDirections.FindRef(St.BeatId);
    if (Out.IsEmpty()) Out = NoStoryDirection;
    for (const FString& SideId : St.Side)
    {
        if (const FString* Extra = SideDirections.Find(SideId))
        {
            Out += TEXT(" ") + *Extra;
        }
    }
    return Out;
}

FString AVillagerCharacter::RespondToChat(const FString& PlayerText)
{
    if (bIsDead) return FString();

    FString Reply;
    if (ContainsAny(PlayerText, DirectionKeywords))
    {
        Reply = BuildDirections();
    }
    else
    {
        for (const FVillagerKeywordRule& Rule : KeywordRules)
        {
            if (ContainsAny(PlayerText, Rule.Keywords))
            {
                Reply = PickLine(Rule.Lines);
                break;
            }
        }
        if (Reply.IsEmpty()) Reply = PickLine(DefaultLines);
    }

    // 말 걸면 멈춰 바라보기(Wave 없음) — 도주·복귀 중이면 대사만.
    if (AVillagerAIController* AIC = Cast<AVillagerAIController>(GetController()))
    {
        AIC->GreetNow(UGameplayStatics::GetPlayerPawn(this, 0), /*bWave=*/false);
    }
    Say(Reply);
    return Reply;
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
