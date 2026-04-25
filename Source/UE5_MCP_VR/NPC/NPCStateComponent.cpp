#include "NPCStateComponent.h"
#include "Action/SmartNPCAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
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
    check(OwnerNPC && "[NPCStateComponent] Owner is not ASmartNPC");
    return OwnerNPC->NPCAttributes;
}

// --- Facial Expression ---

void UNPCStateComponent::SetFacialExpression(EFacialState NewExpression)
{
    if (CurrentFacialState == NewExpression) return;

    CurrentFacialState = NewExpression;

    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
        {
            BB->SetValueAsEnum(ASmartNPCAIController::Key_FacialState, (uint8)CurrentFacialState);
        }
    }

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
    int32 PerceptionBonus = GetAttributes().BaseStats.Perception;
    int32 Roll = UDiceSystem::RollD100();
    int32 Total = Roll + PerceptionBonus;

    bool bSuccess = Total >= Difficulty;

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Reflex Check: Roll(%d) + Perception(%d) = %d vs DC(%d) → %s"),
        Roll, PerceptionBonus, Total, Difficulty, bSuccess ? TEXT("SUCCESS") : TEXT("FAIL"));

    return bSuccess;
}

// --- Damage ---

float UNPCStateComponent::ApplyDamage(float DamageAmount)
{
    FNPCAttributes& Attrs = GetMutableAttributes();
    float EffectiveDamage = FMath::Max(0.0f, DamageAmount - Attrs.Combat.Defense);
    Attrs.Resources.Health -= EffectiveDamage;

    if (UWorld* World = GetWorld())
    {
        LastHitTime = World->GetTimeSeconds();
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Damage Applied: %.1f (Raw: %.1f, Defense: %.1f). HP: %.0f/%.0f"),
        EffectiveDamage, DamageAmount, Attrs.Combat.Defense,
        Attrs.Resources.Health, Attrs.Resources.MaxHealth);

    if (!Attrs.Resources.IsAlive())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCState] NPC is DEAD!"));
        // TODO: Death Event Broadcast (Delegate 등)
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

    // 3. 고위험/저위험 데이터 교차 추출 (최대 4개)
    TArray<FPerceptionData> RefinedEvents;
    int32 Count = LocalEventQueue.Num();

    if (Count <= 4) {
        RefinedEvents = LocalEventQueue;
    } else {
        RefinedEvents.Add(LocalEventQueue[0]);          // Highest
        RefinedEvents.Add(LocalEventQueue[Count - 1]);  // Lowest
        RefinedEvents.Add(LocalEventQueue[1]);          // 2nd Highest
        RefinedEvents.Add(LocalEventQueue[Count - 2]);  // 2nd Lowest
    }

    FString Payload = UMCPJsonUtils::SerializePerceptionReport(OwnerNPC->AgentID, RefinedEvents);
    if (UNPCManager* Manager = OwnerNPC->GetGameInstance()->GetSubsystem<UNPCManager>())
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
