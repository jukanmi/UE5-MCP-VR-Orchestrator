#include "NPCStateComponent.h"
#include "Action/SmartNPCAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "../Utils/DiceSystem.h"
#include "SmartNPC.h"
#include "NPCManager.h"
#include "Engine/GameInstance.h"

UNPCStateComponent::UNPCStateComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNPCStateComponent::BeginPlay()
{
    Super::BeginPlay();

    RefreshStats();
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
    CurrentStats.RecalculateCombatStats();

    ApplyMovementSpeed();

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Stats Refreshed. HP: %.0f/%.0f, ATK: %.0f"),
        CurrentStats.Resources.Health,
        CurrentStats.Resources.MaxHealth,
        CurrentStats.Combat.AttackPower);
}

void UNPCStateComponent::ApplyMovementSpeed()
{
    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (!OwnerChar) return;
    
    UCharacterMovementComponent* CMC = OwnerChar->GetCharacterMovement();
    if (!CMC) return;
    
    CMC->MaxWalkSpeed = CurrentStats.Movement.WalkSpeed;
}

// --- Reflex ---

bool UNPCStateComponent::TryReflexAction(int32 Difficulty)
{
    int32 PerceptionBonus = CurrentStats.BaseStats.Perception;
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
    float EffectiveDamage = FMath::Max(0.0f, DamageAmount - CurrentStats.Combat.Defense);
    CurrentStats.Resources.Health -= EffectiveDamage;

    if (UWorld* World = GetWorld())
    {
        LastHitTime = World->GetTimeSeconds();
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Damage Applied: %.1f (Raw: %.1f, Defense: %.1f). HP: %.0f/%.0f"),
        EffectiveDamage, DamageAmount, CurrentStats.Combat.Defense,
        CurrentStats.Resources.Health, CurrentStats.Resources.MaxHealth);

    if (!CurrentStats.Resources.IsAlive())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCState] NPC is DEAD!"));
        // TODO: Death Event Broadcast (Delegate 등)
    }

    return EffectiveDamage;
}

// --- Emergency Cognition ---

void UNPCStateComponent::RequestEmergencyCognition(const FString& EventType, const FString& Description)
{
    // 긴급 상황 시 즉시 LLM으로 직접 패킷을 쏘는 대신 Event Debouncing을 위해 Manager에 등록
    UE_LOG(LogTemp, Warning, TEXT("[NPCState] EMERGENCY COGNITION Flagged: %s - %s"), *EventType, *Description);

    ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetOwner());
    if (!OwnerNPC) return;

    UGameInstance* GI = OwnerNPC->GetGameInstance();
    if (!GI) return;

    UNPCManager* NPCManager = GI->GetSubsystem<UNPCManager>();
    if (!NPCManager) return;

                NPCManager->RegisterEmergencyEvent(OwnerNPC->AgentID, EventType, Description);
}

// --- Internal Helper ---

ASmartNPCAIController* UNPCStateComponent::GetOwnerAIController() const
{
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn) return nullptr;
    
        return Cast<ASmartNPCAIController>(OwnerPawn->GetController());
}
