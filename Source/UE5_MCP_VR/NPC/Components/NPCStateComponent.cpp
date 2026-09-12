#include "NPC/Components/NPCStateComponent.h"
#include "NPC/Action/NPCActionComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "NPC/BP/SmartNPC.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Engine/GameInstance.h"
#include "Network/MCPJsonUtils.h"

UNPCStateComponent::UNPCStateComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNPCStateComponent::BeginPlay()
{
    Super::BeginPlay();

    RefreshStats();
}

FNPCAttributes UNPCStateComponent::GetAttributes() const
{
    if (ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetOwner()))
    {
        return OwnerNPC->NPCAttributes;
    }
    return FNPCAttributes();
}

FNPCAttributes& UNPCStateComponent::GetMutableAttributes()
{
    ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetOwner());
    if (!OwnerNPC)
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCStateComponent] Owner is not ASmartNPC — returning default attributes"));
        static FNPCAttributes Fallback;
        return Fallback;
    }
    return OwnerNPC->NPCAttributes;
}

// --- Facial Expression ---

void UNPCStateComponent::SetFacialExpression(EFacialState NewExpression)
{
    if (CurrentFacialState == NewExpression) return;

    CurrentFacialState = NewExpression;
    UE_LOG(LogTemp, Verbose, TEXT("[NPCState] Facial Expression Updated: %d"), (int32)CurrentFacialState);
}

// --- Stats ---

void UNPCStateComponent::RefreshStats()
{
    FNPCAttributes& Attrs = GetMutableAttributes();
    Attrs.RecalculateCombatStats();

    ApplyMovementSpeed();

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Stats Refreshed. HP: %.0f/%.0f, ATK: %.0f"),
        Attrs.Resources.Health,
        Attrs.Resources.MaxHealth,
        Attrs.Combat.AttackPower);
}

void UNPCStateComponent::ApplyMovementSpeed()
{
    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (!OwnerChar) return;
    
    UCharacterMovementComponent* CMC = OwnerChar->GetCharacterMovement();
    if (!CMC) return;
    
    CMC->MaxWalkSpeed = GetAttributes().Movement.WalkSpeed;
}

// --- Plan ---

void UNPCStateComponent::SetCurrentPlan(const FNPCPlan& NewPlan)
{
    CurrentPlan = NewPlan;
    CurrentPlan.bIsValid = true;
    bDangerReplanPending = false;
    bPlanAchievedPending = false;
    TurnsOnCurrentPlan = 0;
    UE_LOG(LogTemp, Log, TEXT("[NPCState] %s: plan 갱신 goal=\"%s\" steps=%d"),
        *GetOwner()->GetName(), *CurrentPlan.Goal, CurrentPlan.Steps.Num());
    OnPlanUpdated.Broadcast(CurrentPlan);
}

// --- Damage ---

float UNPCStateComponent::ApplyDamage(float DamageAmount, float Multiplier)
{
    FNPCAttributes& Attrs = GetMutableAttributes();
    float EffectiveDamage = FMath::Max(0.0f, DamageAmount - Attrs.Combat.Defense) * Multiplier;
    Attrs.Resources.Health -= EffectiveDamage;

    if (UWorld* World = GetWorld())
    {
        LastHitTime = World->GetTimeSeconds();
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Damage Applied: %.1f (Raw: %.1f, Defense: %.1f, x%.2f). HP: %.0f/%.0f"),
        EffectiveDamage, DamageAmount, Attrs.Combat.Defense, Multiplier,
        Attrs.Resources.Health, Attrs.Resources.MaxHealth);

    if (!Attrs.Resources.IsAlive())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCState] %s HP 소진 — SmartNPC::HandleDeath 위임"),
            *GetOwner()->GetName());
    }

    return EffectiveDamage;
}

// --- Event Cognition ---

void UNPCStateComponent::RequestEventCognition(const FPerceptionData& Perception)
{
    ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetOwner());
    if (!OwnerNPC) return;

    LocalEventQueue.Add(Perception);

    // 0.3초 타이머 (이미 돌고 있다면 무시하여 0.3초 내의 모든 이벤트를 모음)
    if (UWorld* World = GetWorld())
    {
        if (!World->GetTimerManager().IsTimerActive(EventDebounceTimer))
        {
            World->GetTimerManager().SetTimer(
                EventDebounceTimer,
                this, &UNPCStateComponent::FlushEventReport,
                0.3f,
                false
            );
        }
    }
}

void UNPCStateComponent::FlushEventReport()
{
    // 1. 타이머 및 유효성 검사
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(EventDebounceTimer);
    ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetOwner());
    if (!OwnerNPC || LocalEventQueue.IsEmpty()) return;

    // 2. 위험도 기준 내림차순 정렬
    LocalEventQueue.Sort([](const FPerceptionData& A, const FPerceptionData& B) {
        return A.DangerScore > B.DangerScore;
    });

    // 3. 위험도 상위 4개만 추출 — LLM 컨텍스트는 가장 위험한 이벤트가 우선
    TArray<FPerceptionData> RefinedEvents;
    const int32 TopN = FMath::Min(4, LocalEventQueue.Num());
    for (int32 i = 0; i < TopN; ++i)
    {
        RefinedEvents.Add(LocalEventQueue[i]);
    }

    // 반사 이력 동봉 — 통보와 같은 배로 보내야 다음 replan 이 "이미 반응함"에서 출발한다.
    FString Payload = UMCPJsonUtils::SerializePerceptionReport(
        OwnerNPC->AgentID, RefinedEvents, TEXT(""), PendingReflexAction);
    if (Payload.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCState] %s: Perception 직렬화 실패 — flush 건너뜀"), *OwnerNPC->AgentID);
        LocalEventQueue.Empty();
        return;
    }
    // location_decision 응답 대기 중에는 Event Report 전송 금지.
    // 같은 LLM WebSocket으로 두 요청이 겹치면 location_decision_result가 타임아웃으로 유실됨.
    if (UNPCActionComponent* ActionComp = OwnerNPC->GetActionComponent())
    {
        if (ActionComp->TacticalQueryState == ETacticalQueryState::WaitingLLM)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[NPCState] %s: Event Report 지연 — location_decision 응답 대기 중"), *OwnerNPC->AgentID);
            return; // LocalEventQueue 유지 → 다음 FlushEventReport 호출 시 재시도
        }
    }

    // 월드 종료/레벨 전환 중 null 가능 — 체인 크래시 방지(Get 이 내부 null 가드).
    UNPCManager* Manager = UNPCManager::Get(OwnerNPC);
    if (!Manager)
    {
        return;
    }
    // 단일 LLM WebSocket으로 emergency_report 전송.
    // Python 서버가 envelope 타입을 보고 내부에서 SLM Reflex/LLM 전략으로 자동 라우팅한다.
    Manager->SendEventReport(OwnerNPC->AgentID, Payload);
    LocalEventQueue.Empty();
    PendingReflexAction.Reset(); // 실제 발신된 뒤에만 소비 — WaitingLLM 지연 시엔 다음 flush 로 이월
}

void UNPCStateComponent::ReportCombatVictory(const FString& DefeatedTargetID)
{
    ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetOwner());
    if (!OwnerNPC) return;

    UNPCManager* Manager = UNPCManager::Get(OwnerNPC);
    if (!Manager) return;

    // 단발 이벤트라 디바운스 큐 미경유. danger=0 — Python 게이트는 report_type 으로 식별.
    FPerceptionData Victory;
    Victory.TargetID = DefeatedTargetID;
    Victory.SenseType = ESenseType::Other;
    Victory.Location = OwnerNPC->GetActorLocation();
    Victory.Distance = 0.f;
    Victory.DangerScore = 0.f;

    const FString Payload = UMCPJsonUtils::SerializePerceptionReport(
        OwnerNPC->AgentID, { Victory }, TEXT("combat_victory"));
    if (Payload.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCState] %s: 승리 보고 직렬화 실패 — 전송 생략"), *OwnerNPC->AgentID);
        return;
    }

    Manager->SendEventReport(OwnerNPC->AgentID, Payload);
    UE_LOG(LogTemp, Log, TEXT("[NPCState] %s: 전투 승리 보고 전송 (defeated=%s)"),
        *OwnerNPC->AgentID, *DefeatedTargetID);
}

// --- Affinity ---

void UNPCStateComponent::UpdateAffinity(const FString& TargetID, int32 NewScore)
{
    AffinityCache.Add(TargetID, NewScore);
    UE_LOG(LogTemp, Verbose, TEXT("[NPCState] Updated Affinity for %s -> %s: %d"), 
        *GetOwner()->GetName(), *TargetID, NewScore);
}

ENPCRelation UNPCStateComponent::GetRelation(const FString& TargetID) const
{
    const int32* Score = AffinityCache.Find(TargetID);
    if (!Score)
    {
        return ENPCRelation::Neutral; // 캐시(Target)가 없으면 중립 취급
    }

    if (*Score >= AffinityFriendlyThreshold) return ENPCRelation::Friendly;
    if (*Score <= AffinityHostileThreshold)  return ENPCRelation::Hostile;
    return ENPCRelation::Neutral;
}

float UNPCStateComponent::GetAffinityMultiplier(const FString& TargetID) const
{
    // 관계 판정은 GetRelation 단일 소스. 여기선 배율로 옮기기만 한다.
    switch (GetRelation(TargetID))
    {
    case ENPCRelation::Friendly: return 0.0f; // 우호적: 위협이 아님
    case ENPCRelation::Hostile:  return 1.0f; // 적대적: 최대 위협
    default:                     return AffinityDefaultMultiplier; // 중립·미캐싱
    }
}

void UNPCStateComponent::NoteReflexAction(EAction ReflexAction)
{
    // 한 flush 안에 반사가 여러 번 나면 마지막 것만 남긴다 — LLM 에 필요한 건
    // "이미 최소 반응을 했다"는 사실이지 반사 전수 목록이 아니다.
    PendingReflexAction = UEnum::GetDisplayValueAsText(ReflexAction).ToString();
}
