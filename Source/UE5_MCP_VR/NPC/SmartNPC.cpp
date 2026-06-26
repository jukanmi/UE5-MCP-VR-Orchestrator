#include "SmartNPC.h"
#include "../Core/GameplayTagUtils.h"
#include "NPCManager.h"
#include "Action/SmartNPCAIController.h"
#include "Struct/NPCActionKeys.h"
#include "NPCStateComponent.h"
#include "Action/NPCActionComponent.h"
#include "NPCActionDataAsset.h"
#include "NPCInventoryComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"  // [SPIKE]
#include "Engine/Engine.h"  // [SPIKE] GEngine
#include "Animation/AnimMontage.h"  // 기상 몽타주
#include "Animation/AnimInstance.h"  // Montage_SetEndDelegate / FOnMontageEnded
#include "NPCAudioStreamComponent.h"
#include "../UI/NPCDialogueWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DamageEvents.h"

// 본 이름 → 부위. 본 미식별(None/캡슐 히트)은 Torso 폴백.
// Mixamo X_Bot(RightArm/RightUpLeg/Hips…)·UE Mannequin(upperarm_r/thigh_r/pelvis…) 양 네이밍 수용.
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

static float BodyPartMultiplier(EBodyPartType P)
{
    switch (P)
    {
        case EBodyPartType::Head:  return 2.0f;
        case EBodyPartType::Torso: return 1.0f;
        default:                   return 0.75f;  // ArmLeft/ArmRight/LegLeft/LegRight
    }
}

ASmartNPC::ASmartNPC()
{
    // Tick은 디버그 머리 위 호감도 표시(bShowAffinityOnScreen=true) 시에만 사용.
    // BeginPlay에서 플래그 보고 SetActorTickEnabled로 토글하므로 기본은 꺼둠.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    AgentID = TEXT("UnknownAgent");
    AIControllerClass = ASmartNPCAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    StateComponent     = CreateDefaultSubobject<UNPCStateComponent>(TEXT("StateComponent"));
    ActionComponent    = CreateDefaultSubobject<UNPCActionComponent>(TEXT("ActionComponent"));
    InventoryComponent = CreateDefaultSubobject<UNPCInventoryComponent>(TEXT("InventoryComponent"));

    // [SPIKE] 액티브 래그돌 hit-react — 메시 바인딩은 BeginPlay 에서(GetMesh 준비 후).
    PhysicalAnim = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnim"));

    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    if (StimuliSource)
    {
        StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
        StimuliSource->RegisterForSense(UAISense_Hearing::StaticClass());
        StimuliSource->RegisterWithPerceptionSystem();
    }

    // 머리 위 대사 말풍선 (WorldSpace). WBP 클래스·정밀 위치는 BP 에서.
    DialogueWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("DialogueWidget"));
    DialogueWidgetComp->SetupAttachment(GetMesh());
    DialogueWidgetComp->SetWidgetSpace(EWidgetSpace::World);
    DialogueWidgetComp->SetDrawAtDesiredSize(true);
    DialogueWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 110.f)); // 머리 위 기본값(에디터 튜닝)
    DialogueWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DialogueWidgetComp->SetVisibility(false);

    // --- AI NPC 회전 설정 ---
    // 기본 Character는 컨트롤러 Yaw를 따라 회전(bUseControllerRotationYaw=true)하고
    // 이동 방향을 향하지 않아(bOrientRotationToMovement=false) MoveTo 시 옆걸음/뒷걸음 발생.
    // AI는 컨트롤러 회전 무시 + 이동 방향으로 자동 회전.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;

    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->bOrientRotationToMovement = true;
        CMC->RotationRate = FRotator(0.f, 540.f, 0.f); // Yaw 540°/s — 부드럽고 빠른 선회
        CMC->bUseControllerDesiredRotation = false;
    }
}

void ASmartNPC::BeginPlay()
{
    Super::BeginPlay();

    // [SPIKE] PhysicalAnimation 대상 메시 바인딩 + 기상 후 복원용 원본 콜리전 프로파일 캡처.
    if (PhysicalAnim && GetMesh())
    {
        PhysicalAnim->SetSkeletalMeshComponent(GetMesh());
    }
    if (GetMesh())
    {
        OriginalMeshProfile = GetMesh()->GetCollisionProfileName();
    }

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->RegisterNPC(AgentID, this);
        }
    }

    AddStateTag(FGameplayTag::RequestGameplayTag(FName("State.Idle")));

    // 자막 싱크 — 자기 오디오 컴포넌트의 재생 시작/종료 델리게이트 1회 구독.
    TryBindAudioSubtitle();

    // 계획 갱신 구독 — plan 산출 시 로그 알림. 1회 바인딩.
    if (StateComponent && !bPlanUpdatedBound)
    {
        StateComponent->OnPlanUpdated.AddDynamic(this, &ASmartNPC::HandlePlanUpdated);
        bPlanUpdatedBound = true;
    }

    // 디버그 표시 활성화된 NPC만 Tick 켜기 (대부분 NPC는 Tick 비용 0)
    // 자막·flinch·넉다운 등 다른 소비자는 RefreshTickEnabled 가 OR 로 함께 관리.
    RefreshTickEnabled();
}

void ASmartNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 넉다운 도중 파괴/종료 시 동시 카운트 누수 방지(static — PIE 세션 넘어 잔존).
    if (KnockdownPhase != EKnockdownPhase::None)
    {
        ActiveKnockdownCount = FMath::Max(0, ActiveKnockdownCount - 1);
        KnockdownPhase = EKnockdownPhase::None;
    }

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->UnregisterNPC(AgentID);
        }
    }

    Super::EndPlay(EndPlayReason);
}

// === Facade: 외부 호출을 컴포넌트로 전달 ===

// [의도(Why)] 기존 직접 호출(ExecuteActionBatch)은 INPC 인터페이스 구현체로 통합합니다.
void ASmartNPC::ExecuteActionBatch(const FActionBatch& Batch)
{
    if (ActionComponent)
    {
        ActionComponent->ExecuteActionBatch(Batch);
    }
}



void ASmartNPC::SetBlackboardBool(const FString& KeyName, bool bValue)
{
    ASmartNPCAIController* AICtrl = Cast<ASmartNPCAIController>(GetController());
    if (!AICtrl) return;

    UBlackboardComponent* BlackboardComp = AICtrl->GetBlackboardComponent();
    if (!BlackboardComp) return;

    BlackboardComp->SetValueAsBool(FName(*KeyName), bValue);
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Blackboard key '%s' set to %s on NPC '%s'"),
        *KeyName,
        bValue ? TEXT("true") : TEXT("false"),
        *AgentID);
}


void ASmartNPC::OnActionCompleted()
{
    if (ActionComponent)
    {
        ActionComponent->OnActionCompleted();
    }
}

float ASmartNPC::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (bIsDead) return 0.f;

    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

    if (StateComponent)
    {
        // [의도(Why)] 히트스캔이 채운 본 이름으로 부위를 식별해 부위별 데미지 배율을 적용.
        float Multiplier = 1.0f;
        if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
        {
            const FPointDamageEvent& Pt = static_cast<const FPointDamageEvent&>(DamageEvent);
            Multiplier = BodyPartMultiplier(BoneToBodyPart(Pt.HitInfo.BoneName));
            // 사망 래그돌 임펄스용: 맞은 본 + 발사 방향 캡처(다음 줄 사망 판정에서 소비될 수 있음).
            LastHitBone = Pt.HitInfo.BoneName;
            LastHitDirection = Pt.ShotDirection;
        }
        StateComponent->ApplyDamage(ActualDamage, Multiplier);

        if (!StateComponent->GetAttributes().Resources.IsAlive())
        {
            HandleDeath();
            return ActualDamage;
        }

        // 비치사 피격 → 동역학 반응. 데미지·인지는 이미 적용됨(반응 분기와 무관 — §5.D 데미지 상시).
        // 기본 경로: ReactToHit(강타=Knockdown / 약타=Flinch). bSpikeReactOnHit=true 면 구 스파이크 스냅 비교용.
        if (bSpikeReactOnHit)
        {
            SpikeHitReact();
        }
        else
        {
            ReactToHit(ActualDamage);
        }

        // [의도(Why)] 피격 정보를 인지 이벤트 배칭 시스템으로 전송하여 즉각적인 상황 인지 및 전략적 판단(도주, 반격 등)을 유도합니다.
        FPerceptionData DamageEventPerc;
        DamageEventPerc.TargetID = DamageCauser ? DamageCauser->GetName() : TEXT("Unknown");
        DamageEventPerc.SenseType = ESenseType::Hit;
        DamageEventPerc.Location = DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation();
        DamageEventPerc.Distance = DamageCauser ? FVector::Dist(GetActorLocation(), DamageEventPerc.Location) : 0.0f;
        DamageEventPerc.DangerScore = 1.0f;

        StateComponent->RequestEventCognition(DamageEventPerc);
    }

    return ActualDamage;
}

void ASmartNPC::HandleDeath()
{
    if (bIsDead) return;
    bIsDead = true;

    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] %s 사망 처리 시작"), *AgentID);

    // 1. 게임플레이 태그: 기존 상태 전부 제거 후 Dead 태그 부착
    GameplayTags.Reset();
    AddStateTag(FGameplayTag::RequestGameplayTag(FName("State.Condition.Dead")));

    // 2. 진행 중인 모든 액션 즉시 중지
    if (ActionComponent)
    {
        ActionComponent->StopAllActions();
    }

    // 3. NPCMap에서 즉시 퇴출 — 이후 어떤 LLM 응답도 이 NPC로 전달되지 않음
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->UnregisterNPC(AgentID);
        }
    }

    // 4. AI 컨트롤러 해제 — BT 완전 중단
    if (AController* C = GetController())
    {
        C->UnPossess();
    }

    // 5. 패시브 래그돌 — 사망 몽타주 대신 물리 시뮬로 자연 붕괴(§5 VR 실감형).
    //    진행 중이던 넉다운/기상은 정리(타이머·동시카운트). 이미 시뮬 중이면 EnterRagdoll 이 임펄스만 갱신(§6).
    if (KnockdownPhase != EKnockdownPhase::None)
    {
        GetWorldTimerManager().ClearTimer(GetUpMontageTimer);
        ActiveKnockdownCount = FMath::Max(0, ActiveKnockdownCount - 1);
        KnockdownPhase = EKnockdownPhase::None;
    }
    bFlinching = false;

    // 전신 래그돌 진입(캡슐 NoCollision·CMC 정지·메시 시뮬·치사 임펄스). 전제: 메시 Physics Asset 필수.
    EnterRagdoll(/*bFatal=*/true);
    RefreshTickEnabled();

    // 6. 사망 이벤트 브로드캐스트 — BP에서 VFX 등 추가 연결 가능
    OnNPCDied.Broadcast(this);

    // 7. 일정 시간 후 Actor 제거 (래그돌 안착·시신 노출 여유 시간)
    constexpr float DestroyDelay = 3.f;
    if (UWorld* World = GetWorld())
    {
        FTimerHandle DestroyTimer;
        World->GetTimerManager().SetTimer(DestroyTimer, this, &ASmartNPC::DestroyAfterDeath, DestroyDelay, false);
    }

    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] %s NPCMap 퇴출 완료. %.1f초 후 Actor 제거."), *AgentID, DestroyDelay);
}

void ASmartNPC::DestroyAfterDeath()
{
    Destroy();
}


void ASmartNPC::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    TagContainer = GameplayTags;
}

void ASmartNPC::AddStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::AddState(GameplayTags, Tag);
}

void ASmartNPC::RemoveStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::RemoveState(GameplayTags, Tag);
}

bool ASmartNPC::IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const
{
    if (!StateComponent || !Other.GetObject()) return false;

    const FString OtherID = ICharacterBase::Execute_GetEntityID(Other.GetObject());
    // AffinityHostileThreshold 이하면 적대 관계
    return StateComponent->GetAffinityMultiplier(OtherID) >= 1.0f;
}

void ASmartNPC::Debug_TestPlanHUD()
{
    if (!StateComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlanHUD] %s: StateComponent 없음"), *AgentID);
        return;
    }

    FNPCPlan Test;
    Test.Goal = TEXT("DEBUG TEST PLAN");
    Test.Steps = { TEXT("step1"), TEXT("step2") };
    StateComponent->SetCurrentPlan(Test); // → OnPlanUpdated.Broadcast → HandlePlanUpdated 로그
}

void ASmartNPC::Debug_PrintAffinity()
{
    if (!StateComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Affinity] %s: StateComponent 없음"), *AgentID);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("=== [Affinity] %s (Friendly>=%d, Hostile<=%d) ==="),
        *AgentID, StateComponent->AffinityFriendlyThreshold, StateComponent->AffinityHostileThreshold);

    if (StateComponent->AffinityCache.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("  (캐시 비어있음 — 아직 state_update를 받지 못함)"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow,
                FString::Printf(TEXT("[%s] Affinity: 캐시 없음"), *AgentID));
        }
        return;
    }

    for (const TPair<FString, int32>& Pair : StateComponent->AffinityCache)
    {
        const float Mult = StateComponent->GetAffinityMultiplier(Pair.Key);
        FString Relation = TEXT("Neutral");
        if (Pair.Value >= StateComponent->AffinityFriendlyThreshold) Relation = TEXT("Friendly");
        else if (Pair.Value <= StateComponent->AffinityHostileThreshold) Relation = TEXT("Hostile");

        UE_LOG(LogTemp, Warning, TEXT("  %s: score=%d (%s, multiplier=%.2f)"),
            *Pair.Key, Pair.Value, *Relation, Mult);

        if (GEngine)
        {
            FColor LineColor = FColor::White;
            if (Relation == TEXT("Friendly")) LineColor = FColor::Green;
            else if (Relation == TEXT("Hostile")) LineColor = FColor::Red;

            GEngine->AddOnScreenDebugMessage(-1, 8.f, LineColor,
                FString::Printf(TEXT("[%s→%s] %d (%s, mult=%.2f)"),
                    *AgentID, *Pair.Key, Pair.Value, *Relation, Mult));
        }
    }
}

void ASmartNPC::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 액티브 래그돌 물리 반응 업데이트 — affinity 디버그 플래그와 독립적으로 항상 처리.
    if (bFlinching)
    {
        TickFlinchRamp(DeltaSeconds);
    }
    if (KnockdownPhase == EKnockdownPhase::Ragdoll)
    {
        TickSettleDetection(DeltaSeconds);
    }
    else if (KnockdownPhase == EKnockdownPhase::GettingUp)
    {
        TickGetUpBlend(DeltaSeconds);
    }

    // 말풍선 빌보드 — 표시 중일 때만 플레이어 카메라 향해 Yaw 정렬(텍스트 직립 유지).
    if (DialogueWidgetComp && DialogueWidgetComp->IsVisible())
    {
        if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
        {
            const FVector ToCam = Cam->GetCameraLocation() - DialogueWidgetComp->GetComponentLocation();
            const float Yaw = ToCam.Rotation().Yaw + 180.f; // 위젯 정면이 카메라를 향하도록
            DialogueWidgetComp->SetWorldRotation(FRotator(0.f, Yaw, 0.f));
        }
    }

    if (!bShowAffinityOnScreen || !StateComponent) return;

    UWorld* World = GetWorld();
    if (!World) return;

    // 머리 위 텍스트 오프셋
    const FVector TextOffset(0.f, 0.f, 110.f);
    const FVector BaseLoc = GetActorLocation() + TextOffset;

    if (StateComponent->AffinityCache.Num() == 0)
    {
        DrawDebugString(World, BaseLoc,
            FString::Printf(TEXT("[%s] Affinity: (none)"), *AgentID),
            nullptr, FColor::Yellow, 0.f, true, 0.9f);
        return;
    }

    int32 LineIdx = 0;
    for (const TPair<FString, int32>& Pair : StateComponent->AffinityCache)
    {
        FColor LineColor = FColor::White;
        if (Pair.Value >= StateComponent->AffinityFriendlyThreshold) LineColor = FColor::Green;
        else if (Pair.Value <= StateComponent->AffinityHostileThreshold) LineColor = FColor::Red;

        const FVector LineLoc = BaseLoc + FVector(0.f, 0.f, -15.f * LineIdx);
        DrawDebugString(World, LineLoc,
            FString::Printf(TEXT("%s: %d"), *Pair.Key, Pair.Value),
            nullptr, LineColor, 0.f, true, 0.9f);
        ++LineIdx;
    }
}

// === Dialogue Subtitle (머리 위 WorldSpace 말풍선) ===

void ASmartNPC::TryBindAudioSubtitle()
{
    if (bAudioSubtitleBound) return;

    if (UNPCAudioStreamComponent* Audio = FindComponentByClass<UNPCAudioStreamComponent>())
    {
        Audio->OnAudioStarted.AddDynamic(this, &ASmartNPC::HandleSubtitleAudioStarted);
        Audio->OnAudioCompleted.AddDynamic(this, &ASmartNPC::HandleSubtitleAudioCompleted);
        bAudioSubtitleBound = true;
    }
}

void ASmartNPC::ShowSubtitle(const FString& Text, bool bWaitForAudio)
{
    if (Text.IsEmpty()) return;

    // 액션 dialogue 후 같은 발화의 TTS 가 뒤따라 오는 경우 — 같은 텍스트면 깜빡임 없이 이어감.
    const bool bSameText = (Text == CurrentSubtitleText);
    CurrentSubtitleText = Text;

    // 늦게 첨부된 오디오 컴포넌트 대비 바인딩 재시도.
    TryBindAudioSubtitle();

    if (bWaitForAudio)
    {
        // 음성 시작(HandleSubtitleAudioStarted)이 표시를 맡는다.
        bSubtitleWaitingForAudio = true;

        // 같은 텍스트가 폴백으로 이미 보이는 중이면 유지, 아니면 텍스트만 세팅(숨김) 후 Started 대기.
        const bool bAlreadyVisible = DialogueWidgetComp && DialogueWidgetComp->IsVisible();
        ApplySubtitle(bSameText && bAlreadyVisible);

        // 안전 상한 — 음성이 시작/완료되지 않아도(에러·끊김) 영구 표시 방지.
        GetWorldTimerManager().SetTimer(SubtitleHideTimer, this, &ASmartNPC::HideSubtitle, SubtitleMaxDuration, false);
        return;
    }

    // 즉시 표시 + 길이 비례 폴백 타이머.
    bSubtitleWaitingForAudio = false;
    ApplySubtitle(true);

    const float Duration = SubtitleFallbackDuration + SubtitlePerCharDuration * Text.Len();
    GetWorldTimerManager().ClearTimer(SubtitleHideTimer);
    GetWorldTimerManager().SetTimer(SubtitleHideTimer, this, &ASmartNPC::HideSubtitle, Duration, false);
}

void ASmartNPC::HandleSubtitleAudioStarted()
{
    // 음성 재생 시작 — 대기 중이던 자막 표시.
    if (CurrentSubtitleText.IsEmpty()) return;
    bSubtitleWaitingForAudio = false;
    ApplySubtitle(true);
    // Completed 정상 도착 시 숨김. 누락(에러·끊김) 대비 안전 상한 갱신.
    GetWorldTimerManager().SetTimer(SubtitleHideTimer, this, &ASmartNPC::HideSubtitle, SubtitleMaxDuration, false);
}

void ASmartNPC::HandleSubtitleAudioCompleted()
{
    HideSubtitle();
}

void ASmartNPC::HideSubtitle()
{
    bSubtitleWaitingForAudio = false;
    GetWorldTimerManager().ClearTimer(SubtitleHideTimer);
    CurrentSubtitleText.Reset();
    ApplySubtitle(false);
}

void ASmartNPC::ApplySubtitle(bool bVisible)
{
    if (!DialogueWidgetComp) return;

    // 위젯 오브젝트가 아직 생성 전이면(최초 표시) 강제 초기화. WBP 미지정 시 null 유지 — 디버그 자막 폴백.
    if (!DialogueWidgetComp->GetUserWidgetObject())
    {
        DialogueWidgetComp->InitWidget();
    }

    if (UNPCDialogueWidget* W = Cast<UNPCDialogueWidget>(DialogueWidgetComp->GetUserWidgetObject()))
    {
        W->SetDialogue(AgentID, CurrentSubtitleText);
    }

    DialogueWidgetComp->SetVisibility(bVisible);

    // 빌보드용 Tick — 표시 중에만. 다른 소비자(호감도·flinch·넉다운) OR 해 일원 관리.
    RefreshTickEnabled();
}

// === Plan 갱신 로그 알림 ===

void ASmartNPC::HandlePlanUpdated(const FNPCPlan& NewPlan)
{
    UE_LOG(LogTemp, Warning, TEXT("[PlanHUD] %s: plan 갱신 goal=\"%s\" steps=%d"),
        *AgentID, *NewPlan.Goal, NewPlan.Steps.Num());
}

// ====================================================================
// [SPIKE] 액티브 래그돌 hit-react — throwaway 타당성 검증
// 상체(SpikeRootBone 이하)만 물리 시뮬 → PD(PhysicalAnimation)가 애니 포즈로 당김.
// 하체(Hips/다리)는 애니 유지 → 이동 지속. 임펄스로 움찔 후 SpikeRecoverTime 후 복귀.
// 전제: 메시에 PA_SmartNPC(물리에셋) 할당 필수.
// ====================================================================

void ASmartNPC::SpikeHitReact()
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp || !PhysicalAnim || bIsDead)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Spike] mesh/PhysicalAnim 없음 또는 사망"));
        return;
    }

    // 1) PD 강도 — 시뮬 본을 매 프레임 애니 포즈(kinematic 타겟)로 끌어당김.
    FPhysicalAnimationData Data;
    Data.bIsLocalSimulation     = false;
    Data.OrientationStrength     = SpikeOrientationStrength;
    Data.AngularVelocityStrength = SpikeAngularVelStrength;
    Data.PositionStrength        = 0.f;
    Data.VelocityStrength        = 0.f;
    Data.MaxLinearForce          = 0.f;
    Data.MaxAngularForce         = 0.f;
    PhysicalAnim->ApplyPhysicalAnimationSettingsBelow(SpikeRootBone, Data, /*bIncludeSelf=*/true);

    // 2) 메시 콜리전 물리 허용 — 기본 QueryOnly 면 AddImpulse 거부됨(경고). 복귀 시 원복.
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    // 3) 상체 물리 ON + 블렌드 1.0(완전 물리, PD 가 애니로 복원).
    MeshComp->SetAllBodiesBelowSimulatePhysics(SpikeRootBone, true, /*bIncludeSelf=*/true);
    MeshComp->SetAllBodiesBelowPhysicsBlendWeight(SpikeRootBone, 1.0f, /*bSkipCustomPhysicsType=*/false, /*bIncludeSelf=*/true);

    // 4) 피격 임펄스 — 가슴(Spine2)에 뒤로. 실제론 타격 방향(LastHitDirection) 사용.
    MeshComp->AddImpulse(GetActorForwardVector() * -SpikeImpulse, TEXT("Spine2"), /*bVelChange=*/false);

    UE_LOG(LogTemp, Log, TEXT("[Spike] %s hit-react ON (root=%s, recover %.2fs)"),
        *AgentID, *SpikeRootBone.ToString(), SpikeRecoverTime);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Magenta,
            FString::Printf(TEXT("[Spike] %s hit-react"), *AgentID));
    }

    // 5) 복귀 타이머.
    GetWorldTimerManager().SetTimer(SpikeRecoverTimer, this, &ASmartNPC::SpikeRecover, SpikeRecoverTime, false);
}

void ASmartNPC::SpikeRecover()
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp || bIsDead) return;
    // 블렌드 0(순수 애니) + 시뮬 OFF + 콜리전 QueryOnly 원복. (스파이크라 스냅 — 실제는 Tick 램프)
    MeshComp->SetAllBodiesBelowPhysicsBlendWeight(SpikeRootBone, 0.0f, false, true);
    MeshComp->SetAllBodiesBelowSimulatePhysics(SpikeRootBone, false, true);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    UE_LOG(LogTemp, Log, TEXT("[Spike] %s recovered to anim"), *AgentID);
}

// ====================================================================
// 액티브 래그돌 (§3) — 트리거형 hit-react. 약타=Flinch(상체 PD 복귀), 강타=Knockdown(전신 래그돌→기상).
// ====================================================================

int32 ASmartNPC::ActiveKnockdownCount = 0;

void ASmartNPC::RefreshTickEnabled()
{
    const bool bWantTick =
        bShowAffinityOnScreen
        || (DialogueWidgetComp && DialogueWidgetComp->IsVisible())
        || bFlinching
        || (KnockdownPhase != EKnockdownPhase::None);
    SetActorTickEnabled(bWantTick);
}

// 사망·넉다운 공유 — 전신 래그돌 진입(HandleDeath step5 추출). 전제: 메시 Physics Asset 필수.
void ASmartNPC::EnterRagdoll(bool bFatal)
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;

    // 진행 중 몽타주 정지(애니가 물리와 위치 다툼 방지).
    StopAnimMontage();

    // 캡슐 충돌 끄기(래그돌이 자기 캡슐에 걸려 뜨는 것 방지).
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    // 이동 컴포넌트 정지(물리와 위치 다툼 방지).
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->StopMovementImmediately();
        CMC->DisableMovement();
    }

    // 순수 래그돌 — 직전 Flinch 가 남긴 상체 PD 제거(§6: PD 가 서기 애니로 당기면 낙하 충돌).
    // 기본 FPhysicalAnimationData 는 전 강도 0 → 사실상 PD off.
    if (PhysicalAnim)
    {
        PhysicalAnim->ApplyPhysicalAnimationSettingsBelow(KnockdownPelvisBone, FPhysicalAnimationData(), /*bIncludeSelf=*/true);
    }
    FlinchBlendWeight = 0.f;

    // 전신 물리 시뮬 ON + 블렌드 1(완전 물리). 기상 시 블렌드를 0 으로 램프.
    MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComp->SetAllBodiesSimulatePhysics(true);
    MeshComp->SetSimulatePhysics(true);
    MeshComp->SetAllBodiesPhysicsBlendWeight(1.0f);
    MeshComp->WakeAllRigidBodies();

    // 마지막 타격 방향 임펄스 — 사망=DeathImpulseStrength, 넉다운=×KnockdownImpulseScale.
    const float ImpulseMag = bFatal ? DeathImpulseStrength : (DeathImpulseStrength * KnockdownImpulseScale);
    if (ImpulseMag > 0.f && !LastHitDirection.IsNearlyZero())
    {
        const FName ImpulseBone = (LastHitBone != NAME_None) ? LastHitBone : MeshComp->GetBoneName(0);
        MeshComp->AddImpulse(LastHitDirection * ImpulseMag, ImpulseBone, /*bVelChange=*/false);
    }
}

// 피격 강도(=½mv² 데미지)로 반응 분기. 방향·본은 직전 TakeDamage 가 채운 LastHit* 사용.
void ASmartNPC::ReactToHit(float HitStrength)
{
    if (bIsDead) return;

    if (HitStrength >= KnockdownImpulseThreshold)
    {
        Knockdown();
    }
    else
    {
        // Flinch 내부에서 넉다운/기상 중이면 무시(약타는 강반응 덮어쓰지 않음).
        Flinch();
    }
}

// 약타 — 상체(FlinchRootBone 이하) 물리 블렌드 + 임펄스. Tick 램프(TickFlinchRamp)가 애니로 복귀.
void ASmartNPC::Flinch()
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp || !PhysicalAnim || bIsDead) return;

    // 넉다운/기상 중에는 약타 반응 생략(§4) — 전신 래그돌이 우선.
    if (KnockdownPhase != EKnockdownPhase::None) return;

    // 1) PD — 시뮬 본을 매 프레임 애니 포즈로 끌어당김(위치는 자유, 방향만 복원).
    FPhysicalAnimationData Data;
    Data.bIsLocalSimulation     = false;
    Data.OrientationStrength     = FlinchOrientationStrength;
    Data.AngularVelocityStrength = FlinchAngularVelStrength;
    Data.PositionStrength        = 0.f;
    Data.VelocityStrength        = 0.f;
    Data.MaxLinearForce          = 0.f;
    Data.MaxAngularForce         = 0.f;
    PhysicalAnim->ApplyPhysicalAnimationSettingsBelow(FlinchRootBone, Data, /*bIncludeSelf=*/true);

    // 2) 메시 물리 콜리전 허용(QueryOnly 면 AddImpulse 거부) + 상체 시뮬 ON + 블렌드 1.
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComp->SetAllBodiesBelowSimulatePhysics(FlinchRootBone, true, /*bIncludeSelf=*/true);
    MeshComp->SetAllBodiesBelowPhysicsBlendWeight(FlinchRootBone, 1.0f, /*bSkipCustomPhysicsType=*/false, /*bIncludeSelf=*/true);
    FlinchBlendWeight = 1.0f;

    // 3) 피격 임펄스 — 실제 타격 방향. 본 정보 없으면 가슴(Spine2) 폴백.
    const FVector Dir  = LastHitDirection.IsNearlyZero() ? -GetActorForwardVector() : LastHitDirection;
    const FName   Bone = (LastHitBone != NAME_None) ? LastHitBone : FName(TEXT("Spine2"));
    MeshComp->AddImpulse(Dir * FlinchImpulse, Bone, /*bVelChange=*/false);

    bFlinching = true;
    RefreshTickEnabled();
}

// Flinch 복귀 램프 — PhysicsBlendWeight 1→0 보간, 0 도달 시 시뮬 off + 콜리전 원복.
void ASmartNPC::TickFlinchRamp(float DeltaSeconds)
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;

    FlinchBlendWeight = FMath::FInterpConstantTo(FlinchBlendWeight, 0.f, DeltaSeconds, FlinchRecoverSpeed);
    MeshComp->SetAllBodiesBelowPhysicsBlendWeight(FlinchRootBone, FlinchBlendWeight, false, true);

    if (FlinchBlendWeight <= KINDA_SMALL_NUMBER)
    {
        MeshComp->SetAllBodiesBelowSimulatePhysics(FlinchRootBone, false, true);
        MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        bFlinching = false;
        RefreshTickEnabled();
    }
}

// 강타 — 전신 래그돌 + AI 정지 + 안착 후 기상. 넉다운/기상 중 재호출 시 재진입(저글, 가드 없음 §5.C.6).
void ASmartNPC::Knockdown()
{
    if (bIsDead) return;
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;

    const bool bReentry = (KnockdownPhase != EKnockdownPhase::None);

    // 기상 중 재타격 → 몽타주·타이머 취소하고 다시 쓰러뜨림.
    if (KnockdownPhase == EKnockdownPhase::GettingUp)
    {
        StopAnimMontage();
        GetWorldTimerManager().ClearTimer(GetUpMontageTimer);
    }

    if (!bReentry)
    {
        // 동시 넉다운 상한 초과 → Flinch 폴백(전신 래그돌은 비용·시야 혼잡).
        if (ActiveKnockdownCount >= MaxConcurrentKnockdown)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[Ragdoll] %s 동시 넉다운 상한(%d) → Flinch 폴백"),
                *AgentID, MaxConcurrentKnockdown);
            Flinch();
            return;
        }
        ++ActiveKnockdownCount;

        // AI 정지 + 진행 액션 중지(최초 진입만 — 재진입 시 이미 정지).
        if (ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(GetController()))
        {
            AI->PauseAI();
        }
        if (ActionComponent)
        {
            ActionComponent->StopAllActions();
        }
    }

    bFlinching = false;
    KnockdownPhase = EKnockdownPhase::Ragdoll;
    SettleTimer = 0.f;

    // 전신 래그돌(새 임펄스 포함). 재진입이면 이미 시뮬 중 → 임펄스만 갱신됨.
    EnterRagdoll(/*bFatal=*/false);

    RefreshTickEnabled();
}

// 안착 감지 — 골반 선속도가 임계 미만으로 SettleHoldTime 지속되면 기상 시작.
void ASmartNPC::TickSettleDetection(float DeltaSeconds)
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;

    // 골반 본에 피직스 바디가 없으면 GetPhysicsLinearVelocity 가 0 을 반환 → 공중 낙하 중에도
    // 즉시 안착 판정되는 버그. 바디 없으면 루트 바디(NAME_None) 속도로 폴백.
    FName VelBone = KnockdownPelvisBone;
    if (!MeshComp->GetBodyInstance(VelBone))
    {
        VelBone = NAME_None;
    }
    const FVector PelvisVel = MeshComp->GetPhysicsLinearVelocity(VelBone);
    if (PelvisVel.Size() < SettleSpeedThreshold)
    {
        SettleTimer += DeltaSeconds;
        if (SettleTimer >= SettleHoldTime)
        {
            BeginGetUp();
        }
    }
    else
    {
        SettleTimer = 0.f;
    }
}

// 기상 준비 — 엎/누움 판정, 캡슐 바닥 재배치·콜리전 복원, 기상 몽타주 재생 + 블렌드 램프 시작.
void ASmartNPC::BeginGetUp()
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;

    KnockdownPhase = EKnockdownPhase::GettingUp;

    // 1) 엎/누움 판정 — Hips 본의 up 축과 월드 up 내적. >=0 이면 등이 바닥(FaceUp).
    const FVector HipsUp = MeshComp->GetBoneQuaternion(KnockdownPelvisBone).GetUpVector();
    const bool bFaceUp = FVector::DotProduct(HipsUp, FVector::UpVector) >= 0.f;

    // 2) 캡슐 재배치 — Hips 수평 위치, 바닥 트레이스 Z + 캡슐 반높이.
    const FVector HipsLoc = MeshComp->GetBoneLocation(KnockdownPelvisBone);
    float GroundZ = HipsLoc.Z;
    if (UWorld* W = GetWorld())
    {
        FHitResult Hit;
        const FVector Start = HipsLoc + FVector(0.f, 0.f, 100.f);
        const FVector End   = HipsLoc - FVector(0.f, 0.f, 500.f);
        FCollisionQueryParams Params(FName(TEXT("GetUpFloor")), /*bTraceComplex=*/false, this);
        // 정적 지형만 — Visibility 면 타 NPC/플레이어/트리거 위로 텔레포트 위험.
        if (W->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
        {
            GroundZ = Hit.Location.Z;
        }
    }
    float HalfHeight = 88.f;
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    }
    const FVector NewLoc(HipsLoc.X, HipsLoc.Y, GroundZ + HalfHeight);

    // 콜리전 복원 전에 위치 세팅(끼임 방지). 물리 텔레포트로 sweep 생략.
    SetActorLocation(NewLoc, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->SetMovementMode(MOVE_Walking);
    }

    // 3) 기상 몽타주 + 전신 블렌드 램프(시뮬→애니). 현재 블렌드 1 에서 Tick 이 0 으로.
    GetUpBlendWeight = 1.0f;
    UAnimMontage* Montage = bFaceUp ? GetUpMontage_FaceUp : GetUpMontage_FaceDown;
    if (Montage)
    {
        const float Dur = PlayAnimMontage(Montage);
        // 정상 종료는 EndDelegate 로 감지(타이머는 배속·인터럽트에 어긋남). bInterrupted 는 무시.
        if (UAnimInstance* Anim = (GetMesh() ? GetMesh()->GetAnimInstance() : nullptr))
        {
            FOnMontageEnded EndDel;
            EndDel.BindUObject(this, &ASmartNPC::OnGetUpMontageEnded);
            Anim->Montage_SetEndDelegate(EndDel, Montage);
        }
        // 워치독 — 델리게이트 누락·타 몽타주에 의한 인터럽트로 GettingUp 영구 고착 방지.
        GetWorldTimerManager().SetTimer(GetUpMontageTimer, this, &ASmartNPC::FinishGetUp,
            FMath::Max(Dur, 0.1f) + 0.5f, false);
    }
    else
    {
        // 폴백(§8) — 몽타주 미할당 시 즉시 블렌드 복귀. 짧은 타이머로 마무리.
        GetWorldTimerManager().SetTimer(GetUpMontageTimer, this, &ASmartNPC::FinishGetUp, 0.5f, false);
    }

    UE_LOG(LogTemp, Log, TEXT("[Ragdoll] %s 기상 시작 (faceUp=%d, montage=%s)"),
        *AgentID, bFaceUp ? 1 : 0, Montage ? *Montage->GetName() : TEXT("none(fallback)"));

    RefreshTickEnabled();
}

// 기상 블렌드 램프 — 전신 PhysicsBlendWeight 1→0, 0 도달 시 시뮬 off.
void ASmartNPC::TickGetUpBlend(float DeltaSeconds)
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;

    GetUpBlendWeight = FMath::FInterpConstantTo(GetUpBlendWeight, 0.f, DeltaSeconds, FlinchRecoverSpeed);
    MeshComp->SetAllBodiesPhysicsBlendWeight(GetUpBlendWeight);

    if (GetUpBlendWeight <= KINDA_SMALL_NUMBER)
    {
        MeshComp->SetAllBodiesSimulatePhysics(false);
        MeshComp->SetSimulatePhysics(false);
    }
}

// 기상 몽타주 종료 델리게이트 — 정상 완료만 처리. 인터럽트(재넉다운·타 몽타주)는 무시.
void ASmartNPC::OnGetUpMontageEnded(UAnimMontage* /*Montage*/, bool bInterrupted)
{
    if (bInterrupted) return;
    if (KnockdownPhase == EKnockdownPhase::GettingUp)
    {
        FinishGetUp();
    }
}

// 기상 완료 — 메시 콜리전 원복, AI 재개, 평상 복귀. (몽타주 종료 델리게이트·워치독·폴백에서 호출)
void ASmartNPC::FinishGetUp()
{
    if (bIsDead) return;
    GetWorldTimerManager().ClearTimer(GetUpMontageTimer);

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        MeshComp->SetAllBodiesPhysicsBlendWeight(0.f);
        MeshComp->SetAllBodiesSimulatePhysics(false);
        MeshComp->SetSimulatePhysics(false);
        // 원본 프로파일 복원(보통 QueryOnly 캐릭터 메시) — Ragdoll 프로파일 잔존 방지.
        MeshComp->SetCollisionProfileName(OriginalMeshProfile);
    }

    if (KnockdownPhase != EKnockdownPhase::None)
    {
        KnockdownPhase = EKnockdownPhase::None;
        ActiveKnockdownCount = FMath::Max(0, ActiveKnockdownCount - 1);
    }
    SettleTimer = 0.f;
    GetUpBlendWeight = 0.f;

    // AI 재개 — StateTree 재시작(루트부터 재평가 = 기상 후 위협 재판단).
    if (ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(GetController()))
    {
        AI->ResumeAI();
    }

    UE_LOG(LogTemp, Log, TEXT("[Ragdoll] %s 기상 완료 — AI 재개"), *AgentID);
    RefreshTickEnabled();
}
