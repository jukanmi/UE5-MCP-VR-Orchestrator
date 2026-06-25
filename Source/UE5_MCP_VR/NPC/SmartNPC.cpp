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
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Components/WidgetComponent.h"
#include "NPCAudioStreamComponent.h"
#include "../UI/NPCDialogueWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DamageEvents.h"

// UE5 Mannequin(X_Bot) 본 이름 → 부위. 본 미식별(None/캡슐 히트)은 Torso 폴백.
static EBodyPartType BoneToBodyPart(FName Bone)
{
    const FString B = Bone.ToString().ToLower();
    if (B.IsEmpty()) return EBodyPartType::Torso;
    if (B.Contains(TEXT("head")) || B.Contains(TEXT("neck"))) return EBodyPartType::Head;
    if (B.Contains(TEXT("spine")) || B.Contains(TEXT("pelvis")) || B.Contains(TEXT("clavicle")))
        return EBodyPartType::Torso;
    return EBodyPartType::Limb;  // upperarm/lowerarm/hand/thigh/calf/foot
}

static float BodyPartMultiplier(EBodyPartType P)
{
    switch (P)
    {
        case EBodyPartType::Head: return 2.0f;
        case EBodyPartType::Limb: return 0.75f;
        default:                  return 1.0f;  // Torso
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
    // 자막 표시 중에는 ApplySubtitle 가 Tick 을 따로 켠다(빌보드).
    SetActorTickEnabled(bShowAffinityOnScreen);
}

void ASmartNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
        }
        StateComponent->ApplyDamage(ActualDamage, Multiplier);

        if (!StateComponent->GetAttributes().Resources.IsAlive())
        {
            HandleDeath();
            return ActualDamage;
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

    // 5. 사망 몽타주 재생
    if (ActionComponent && ActionComponent->ActionData)
    {
        if (FActionMediaData* M = ActionComponent->ActionData->ActionMedias.Find(NPCActionKeys::Media_Death))
        {
            if (M->Montage) PlayAnimMontage(M->Montage);
        }
    }

    // 6. 사망 이벤트 브로드캐스트 — BP에서 VFX 등 추가 연결 가능
    OnNPCDied.Broadcast(this);

    // 7. 일정 시간 후 Actor 제거 (사망 애니메이션 재생 여유 시간)
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

    // 빌보드용 Tick — 표시 중에만. 숨김 시 디버그(호감도) 표시 설정값으로 복귀.
    SetActorTickEnabled(bVisible || bShowAffinityOnScreen);
}

// === Plan 갱신 로그 알림 ===

void ASmartNPC::HandlePlanUpdated(const FNPCPlan& NewPlan)
{
    UE_LOG(LogTemp, Warning, TEXT("[PlanHUD] %s: plan 갱신 goal=\"%s\" steps=%d"),
        *AgentID, *NewPlan.Goal, NewPlan.Steps.Num());
}
