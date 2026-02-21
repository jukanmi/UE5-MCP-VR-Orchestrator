#include "NPCStateComponent.h"
#include "../AI/SmartNPCAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "../Utils/DiceSystem.h"

UNPCStateComponent::UNPCStateComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNPCStateComponent::BeginPlay()
{
    Super::BeginPlay();

    // 초기 스탯 계산 및 이동속도 적용
    RefreshStats();
}

// --- Facial Expression ---

void UNPCStateComponent::SetFacialExpression(EFacialState NewExpression)
{
    // 중복 업데이트 방지: 같은 표정이면 무시
    if (CurrentFacialState == NewExpression) return;

    CurrentFacialState = NewExpression;

    // Blackboard 동기화 (AnimBP 또는 BT에서 참조)
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
    // Base Stats에서 파생 스탯(전투, 이동 등) 재계산
    CurrentStats.RecalculateCombatStats();

    // 이동속도를 CharacterMovementComponent에 반영
    ApplyMovementSpeed();

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Stats Refreshed. HP: %.0f/%.0f, ATK: %.0f"),
        CurrentStats.Resources.Health,
        CurrentStats.Resources.MaxHealth,
        CurrentStats.Combat.AttackPower);
}

void UNPCStateComponent::ApplyMovementSpeed()
{
    // Owner의 CharacterMovementComponent에 이동속도 적용
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* CMC = OwnerChar->GetCharacterMovement())
        {
            CMC->MaxWalkSpeed = CurrentStats.Movement.WalkSpeed;
            // RunSpeed, SprintSpeed는 BT Task에서 EMoveType에 따라 동적 변경
        }
    }
}

// --- Reflex ---

bool UNPCStateComponent::TryReflexAction(int32 Difficulty)
{
    // 반사 판정: Perception + 주사위(D100) vs Difficulty
    // 왜 Perception인가: 반사 행동은 "위험을 감지"하는 능력에 의존
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
    // 방어력에 따른 데미지 감소 (간단한 flat reduction)
    float EffectiveDamage = FMath::Max(0.0f, DamageAmount - CurrentStats.Combat.Defense);
    CurrentStats.Resources.Health -= EffectiveDamage;

    UE_LOG(LogTemp, Log, TEXT("[NPCState] Damage Applied: %.1f (Raw: %.1f, Defense: %.1f). HP: %.0f/%.0f"),
        EffectiveDamage, DamageAmount, CurrentStats.Combat.Defense,
        CurrentStats.Resources.Health, CurrentStats.Resources.MaxHealth);

    // 사망 판정
    if (!CurrentStats.Resources.IsAlive())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCState] NPC is DEAD!"));
        // Todo: Death Event Broadcast (Delegate 등)
    }

    return EffectiveDamage;
}

// --- Emergency Cognition ---

void UNPCStateComponent::RequestEmergencyCognition(const FString& EventType, const FString& Description)
{
    // 긴급 상황 시 LLM에 재판단 요청
    // 왜 여기에 있는가: 상태(HP 저하, 상태이상 등)에 기반한 트리거이므로
    UE_LOG(LogTemp, Warning, TEXT("[NPCState] EMERGENCY COGNITION: %s - %s"), *EventType, *Description);

    // Todo: WebSocket을 통해 Python Cognitive Engine에 긴급 요청 전송
    // MCPBridge->SendEmergencyRequest(EventType, Description, CurrentStats);
}

// --- Internal Helper ---

ASmartNPCAIController* UNPCStateComponent::GetOwnerAIController() const
{
    if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
    {
        return Cast<ASmartNPCAIController>(OwnerPawn->GetController());
    }
    return nullptr;
}
