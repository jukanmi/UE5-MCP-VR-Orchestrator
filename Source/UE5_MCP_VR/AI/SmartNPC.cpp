#include "SmartNPC.h"
#include "NPCManager.h"
#include "SmartNPCAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "../Utils/MCPMathUtils.h"
#include "Kismet/GameplayStatics.h"

ASmartNPC::ASmartNPC()
{
    PrimaryActorTick.bCanEverTick = true;
    AgentID = TEXT("UnknownAgent");
    AIControllerClass = ASmartNPCAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ASmartNPC::BeginPlay()
{
    Super::BeginPlay();

    // Register self
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->RegisterNPC(AgentID, this);
        }
    }

    // Initialize derived stats and apply movement speeds
    RefreshStats();
}

void ASmartNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Unregister
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->UnregisterNPC(AgentID);
        }
    }

    Super::EndPlay(EndPlayReason);
}

void ASmartNPC::ProcessAction(const FGameAction& Action)
{
    const FString& Type = Action.ActionType;
    const TMap<FString, FString>& P = Action.Parameters;

    ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(GetController());
    if (!AI || !AI->GetBlackboardComponent())
    {
        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] No valid AI Controller or Blackboard provided for %s"), *GetName());
        // Fallback or just return
        return;
    }

    UBlackboardComponent* BB = AI->GetBlackboardComponent();

    // Reset previous command keys if necessary, or just overwrite.
    
    // Resolve Action Type String to Enum
    ESmartNPCActionState ActionState = ESmartNPCActionState::Generic;
    if (Type == TEXT("Move")) ActionState = ESmartNPCActionState::Move;
    else if (Type == TEXT("Speak")) ActionState = ESmartNPCActionState::Speak;
    else if (Type == TEXT("Attack")) ActionState = ESmartNPCActionState::Attack;
    else if (Type == TEXT("Interact")) ActionState = ESmartNPCActionState::Interact;

    // Set ActionType as Enum
    BB->SetValueAsEnum(ASmartNPCAIController::Key_ActionType, (uint8)ActionState);

    if (ActionState == ESmartNPCActionState::Move)
    {
        // Coordinates from ActionBatch are already in Unreal units (from player_location)
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;

        if (const FString* Val = P.Find(TEXT("x"))) X = FCString::Atof(**Val);
        if (const FString* Val = P.Find(TEXT("y"))) Y = FCString::Atof(**Val);
        if (const FString* Val = P.Find(TEXT("z"))) Z = FCString::Atof(**Val);

        // Use coordinates directly - they're already in Unreal units
        FVector TargetLoc(X, Y, Z);
        
        UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Moving to: X=%.1f, Y=%.1f, Z=%.1f"), *AgentID, X, Y, Z);
        
        // Update Blackboard Key
        BB->SetValueAsVector(ASmartNPCAIController::Key_TargetLocation, TargetLoc);
    }
    else if (ActionState == ESmartNPCActionState::Speak)
    {
        // ToDo::말하기 구현 (현재는 BP Event 호출로 처리 중, 필요시 Blackboard 연동)
        
        FString Text = TEXT("...");
        if (const FString* Val = P.Find(TEXT("text"))) Text = *Val;

        ExecuteSpeak(Text);
    }
    else if (ActionState == ESmartNPCActionState::Attack)
    {
        // ToDo::공격 구현 - TargetID를 사용하여 실제 Actor를 찾고 Blackboard Key_TargetActor에 할당하는 로직 필요
        // 예: AActor* Target = FindActorByID(TargetParam);
        // BB->SetValueAsObject(ASmartNPCAIController::Key_TargetActor, Target);
        
        FString TargetParam = Action.TargetID;
        ExecuteAttack(TargetParam);
    }
    else if (ActionState == ESmartNPCActionState::Interact)
    {
        // ToDo::상호작용 구현 - 공격과 마찬가지로 대상 Actor 식별 및 Blackboard 설정 필요
        
        FString TargetParam = Action.TargetID;
        ExecuteInteract(TargetParam);
    }
    else
    {
        // ToDo::기타 일반 액션 처리
        FString TargetParam = Action.TargetID;
        ExecuteGenericAction(Type, TargetParam);
    }
}

void ASmartNPC::ClearPhysicalState()
{
    // Default implementation: Can be overriden by Blueprint or Subclasses.
    // ToDo::상태 초기화 로직 구현 (애니메이션 몽타주 중지, 이동 정지 등)

    // Stop Logic
    StopAnimMontage();
    if (AController* C = GetController())
    {
        C->StopMovement();
    }
}

//
bool ASmartNPC::TryReflexAction(float Difficulty)
{
    // Use Dexterity for reflex checks (Dex 10 = 10% base chance, scaled by difficulty)
    float SuccessChance = static_cast<float>(CurrentStats.BaseStats.Dexterity) - Difficulty;
    float Roll = FMath::RandRange(0.0f, 100.0f);
    
    bool bSuccess = SuccessChance > Roll;
    
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("SmartNPC %s: Reflex SUCCEEDED (Roll: %.1f < %.1f)"), *AgentID, Roll, SuccessChance);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SmartNPC %s: Reflex FAILED (Roll: %.1f >= %.1f)"), *AgentID, Roll, SuccessChance);
        ExecuteEmote("Panic"); 
    }

    return bSuccess;
}

void ASmartNPC::AbortCurrentAction()
{
    UE_LOG(LogTemp, Log, TEXT("SmartNPC %s: ABORTING Action %s"), *AgentID, *CurrentActionID);
    CurrentActionID = TEXT("");
    ClearPhysicalState();
}

void ASmartNPC::RequestEmergencyCognition(FString EventType, FString Description)
{
    AbortCurrentAction();

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            double Time = FPlatformTime::Seconds();
            
            FString JsonPayload = FString::Printf(
                TEXT("{"
                "\"player_id\": \"%s\","
                "\"voice_transcript\": \"[EVENT: %s - %s]\","
                "\"timestamp\": %f,"
                "\"last_event\": \"%s\","
                "\"stats\": {"
                    "\"hp\": %f,"
                    "\"max_hp\": %f,"
                    "\"dexterity\": %d,"
                    "\"perception\": %d"
                "}"
                "}"),
                *AgentID,
                *EventType, *Description,
                Time,
                *EventType,
                CurrentStats.Resources.Health, 
                CurrentStats.Resources.MaxHealth, 
                CurrentStats.BaseStats.Dexterity, 
                CurrentStats.BaseStats.Perception
            );

            Manager->SendEvent(JsonPayload);
        }
    }
}

float ASmartNPC::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    
    // Apply damage to Resources.Health
    CurrentStats.Resources.Health -= ActualDamage;
    if (CurrentStats.Resources.Health < 0) CurrentStats.Resources.Health = 0;

    UE_LOG(LogTemp, Warning, TEXT("SmartNPC %s Took Damage: %.1f. HP: %.1f/%.1f"), 
        *AgentID, ActualDamage, CurrentStats.Resources.Health, CurrentStats.Resources.MaxHealth);

    // Attempt reflex action (difficulty 50)
    bool bReflex = TryReflexAction(50.0f); 
    
    RequestEmergencyCognition(TEXT("Hit"), FString::Printf(TEXT("Took %.1f Damage"), ActualDamage));

    return ActualDamage;
}

void ASmartNPC::ApplyMovementSpeed()
{
    UCharacterMovementComponent* MovementComp = GetCharacterMovement();
    if (!MovementComp)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] %s: No CharacterMovementComponent found!"), *AgentID);
        return;
    }

    // Apply calculated speeds from CharacterAttributes
    // MaxWalkSpeed is the primary speed used by AI navigation
    MovementComp->MaxWalkSpeed = CurrentStats.Movement.WalkSpeed;
    MovementComp->MaxWalkSpeedCrouched = CurrentStats.Movement.CrouchSpeed;
    
    // Note: RunSpeed and SprintSpeed need to be applied via gameplay logic
    // (e.g., setting MaxWalkSpeed dynamically based on movement state)
    
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s: Applied Movement Speeds - Walk: %.1f, Crouch: %.1f (Dex: %d)"),
        *AgentID,
        CurrentStats.Movement.WalkSpeed,
        CurrentStats.Movement.CrouchSpeed,
        CurrentStats.BaseStats.Dexterity);
}

void ASmartNPC::RefreshStats()
{
    // Recalculate all derived stats from base stats
    CurrentStats.RecalculateCombatStats();
    
    // Apply movement speeds to CharacterMovementComponent
    ApplyMovementSpeed();
    
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s: Stats Refreshed - HP: %.1f/%.1f, Walk: %.1f, Run: %.1f, Sprint: %.1f"),
        *AgentID,
        CurrentStats.Resources.Health,
        CurrentStats.Resources.MaxHealth,
        CurrentStats.Movement.WalkSpeed,
        CurrentStats.Movement.RunSpeed,
        CurrentStats.Movement.SprintSpeed);
}
