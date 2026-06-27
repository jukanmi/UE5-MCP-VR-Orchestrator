#include "NPCStateComponent.h"
#include "Action/SmartNPCAIController.h"
#include "Action/NPCActionComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "../Utils/DiceSystem.h"
#include "SmartNPC.h"
#include "NPCManager.h"
#include "Engine/GameInstance.h"
#include "../Network/MCPJsonUtils.h"

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

// --- Reflex ---

bool UNPCStateComponent::TryReflexAction(int32 Difficulty)
{
    int32 PerceptionBonus = FMath::Clamp(GetAttributes().BaseStats.Perception, 0, 100);
    int32 Roll = UDiceSystem::RollD100();
    int32 Total = Roll + PerceptionBonus;

    bool bSuccess = Total >= Difficulty;

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Reflex Check: Roll(%d) + Perception(%d) = %d vs DC(%d) → %s"),
        Roll, PerceptionBonus, Total, Difficulty, bSuccess ? TEXT("SUCCESS") : TEXT("FAIL"));

    return bSuccess;
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

    FString Payload = UMCPJsonUtils::SerializePerceptionReport(OwnerNPC->AgentID, RefinedEvents);
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

    // 월드 종료/레벨 전환 중 GetGameInstance() null 가능 — 체인 크래시 방지
    UGameInstance* GI = OwnerNPC->GetGameInstance();
    if (!GI)
    {
        return;
    }
    if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
    {
        // 단일 LLM WebSocket으로 emergency_report 전송.
        // Python 서버가 envelope 타입을 보고 내부에서 SLM Reflex/LLM 전략으로 자동 라우팅한다.
        Manager->SendEventReport(OwnerNPC->AgentID, Payload);
    }
    LocalEventQueue.Empty();
}

// --- Internal Helper ---

ASmartNPCAIController* UNPCStateComponent::GetOwnerAIController() const
{
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn) return nullptr;
    
    return Cast<ASmartNPCAIController>(OwnerPawn->GetController());
}

// --- Affinity ---

void UNPCStateComponent::UpdateAffinity(const FString& TargetID, int32 NewScore)
{
    AffinityCache.Add(TargetID, NewScore);
    UE_LOG(LogTemp, Verbose, TEXT("[NPCState] Updated Affinity for %s -> %s: %d"), 
        *GetOwner()->GetName(), *TargetID, NewScore);
}

float UNPCStateComponent::GetAffinityMultiplier(const FString& TargetID) const
{
    const int32* Score = AffinityCache.Find(TargetID);
    if (!Score) 
    {
        return AffinityDefaultMultiplier; // 캐시(Target)가 없으면 기본 중립 배율 처리
    }

    if (*Score >= AffinityFriendlyThreshold)
    {
        return 0.0f; // 우호적(Friendly): 위협이 아님
    }
    else if (*Score <= AffinityHostileThreshold)
    {
        return 1.0f; // 적대적(Hostile): 최대 위협
    }
    
    return AffinityDefaultMultiplier; // 중립(Neutral)
}
