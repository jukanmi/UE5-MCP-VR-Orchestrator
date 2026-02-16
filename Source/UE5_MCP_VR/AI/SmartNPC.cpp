#include "SmartNPC.h"
#include "NPCManager.h"
#include "SmartNPCAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "../Utils/MCPMathUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "../Utils/DiceSystem.h"
#include "NPCActionKeys.h"

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

FString ASmartNPC::GetBehaviorModeFromAction(const FString& ActionType) const
{
    if (ActionType.Contains(NPCActionKeys::Action_Attack) || 
        ActionType.Contains(NPCActionKeys::Action_Block) || 
        ActionType.Contains(NPCActionKeys::Action_Dodge))
    {
        return NPCActionKeys::Mode_Combat;
    }
    if (ActionType.Contains(NPCActionKeys::Action_PickUp) || 
        ActionType.Contains(NPCActionKeys::Action_Craft))
    {
        return NPCActionKeys::Mode_Task;
    }
    if (ActionType.Contains(NPCActionKeys::Action_Scan) || 
        ActionType.Contains(NPCActionKeys::Action_Investigate))
    {
        return NPCActionKeys::Mode_Investigation;
    }
    if (ActionType.Contains(NPCActionKeys::Action_Sleep) || 
        ActionType.Contains(NPCActionKeys::Action_Sit))
    {
        return NPCActionKeys::Mode_Lifestyle;
    }
    return NPCActionKeys::Mode_Common;
}

void ASmartNPC::ProcessAction(const FGameAction& Action)
{
    // Wrap generic ProcessAction into the new Batch system for compatibility
    FActionBatch Batch;
    Batch.AgentID = AgentID;
    
    // Simple heuristic to map legacy actions to new Modes
    Batch.BehaviorMode = GetBehaviorModeFromAction(Action.ActionType);

    Batch.FacialState = NPCActionKeys::Value_Neutral; 
    Batch.Actions.Add(Action);

    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] ProcessAction called (Legacy wrapper). Redirecting to ExecuteActionBatch as Mode: %s"), *Batch.BehaviorMode);
    
    ExecuteActionBatch(Batch);
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

bool ASmartNPC::TryReflexAction(int Difficulty)
{
    // Use Dexterity for reflex check via centralized Dice System
    FDiceResult Result;
    bool bSuccess = UDiceSystem::CheckReflex((float)CurrentStats.BaseStats.Dexterity, Difficulty, Result);
    
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("SmartNPC %s: Reflex SUCCEEDED (Roll: %.1f < %.1f)"), *AgentID, Result.RollValue, Result.TargetValue);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SmartNPC %s: Reflex FAILED (Roll: %.1f >= %.1f)"), *AgentID, Result.RollValue, Result.TargetValue);
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


void ASmartNPC::ExecuteActionBatch(const FActionBatch& Batch)
{
    // 1. Update Blackboard State (Behavior Mode, Facial State)
    UpdateBehaviorState(Batch);

    // 2. Dispatch Actions (Queueing or Parallel Execution)
    DispatchActions(Batch.Actions);

    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Executed Batch. Mode: %s, Facial: %s, Actions: %d"), 
        *AgentID, *Batch.BehaviorMode, *Batch.FacialState, Batch.Actions.Num());
}

void ASmartNPC::UpdateBehaviorState(const FActionBatch& Batch)
{
    ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(GetController());
    if (!AI) return;
    UBlackboardComponent* BB = AI->GetBlackboardComponent();
    if (!BB) return;

    // Behavior Mode
    ENPCBehaviorMode Mode = ENPCBehaviorMode::Common; // Default
    const UEnum* ModeEnum = StaticEnum<ENPCBehaviorMode>();
    if (ModeEnum)
    {
        // Try direct name match first, then full scoped name
        int64 Val = ModeEnum->GetValueByName(FName(*Batch.BehaviorMode));
        if (Val == INDEX_NONE)
        {
             Val = ModeEnum->GetValueByName(FName(*FString::Printf(TEXT("ENPCBehaviorMode::%s"), *Batch.BehaviorMode)));
        }
        
        if (Val != INDEX_NONE) Mode = (ENPCBehaviorMode)Val;
    }
    BB->SetValueAsEnum(ASmartNPCAIController::Key_BehaviorMode, (uint8)Mode);

    // Facial State
    EFacialState Facial = EFacialState::Neutral; // Default
    const UEnum* FacialEnum = StaticEnum<EFacialState>();
    if (FacialEnum)
    {
        int64 Val = FacialEnum->GetValueByName(FName(*Batch.FacialState));
        if (Val == INDEX_NONE)
        {
             Val = FacialEnum->GetValueByName(FName(*FString::Printf(TEXT("EFacialState::%s"), *Batch.FacialState)));
        }

        if (Val != INDEX_NONE) Facial = (EFacialState)Val;
    }
    BB->SetValueAsEnum(ASmartNPCAIController::Key_FacialState, (uint8)Facial);
    
    // Sync Local State
    CurrentFacialState = Facial;
}

void ASmartNPC::DispatchActions(const TArray<FGameAction>& Actions)
{
    for (const FGameAction& Action : Actions)
    {
        // 1. Critical Stop Command
        if (Action.ActionType.Equals(NPCActionKeys::Action_Stop, ESearchCase::IgnoreCase))
        {
            StopAllActions();
            continue; 
        }

        // 2. Parallel Action: Dialogue
        if (Action.ActionType.Equals(NPCActionKeys::Action_Dialogue, ESearchCase::IgnoreCase))
        {
            FString TextContent = Action.Parameters.FindRef(NPCActionKeys::Key_Text);
            if (TextContent.IsEmpty()) TextContent = Action.Parameters.FindRef(NPCActionKeys::Key_Content); // Fallback

            FString Emotion = Action.Parameters.FindRef(NPCActionKeys::Key_Emotion);
            if (Emotion.IsEmpty()) Emotion = NPCActionKeys::Value_Neutral;
            
            ExecuteDialogue(TextContent, Emotion);
        }
        else
        {
            // 3. Physical/Sequenced Action -> Enqueue
            ActionQueue.Enqueue(Action);
        }
    }
    
    // Try to start processing if idle
    ProcessNextAction();
}

// --- Debug Functions ---

void ASmartNPC::Debug_ExecuteAction(ENPCBehaviorMode Mode, FString ActionName, FString TargetID, FString Content, FString ExtraParamsJson)
{
    FActionBatch Batch;
    Batch.AgentID = AgentID;
    
    // Convert Enum to String (Remove Prefix for cleaner log, though ExecuteActionBatch handles full name too)
    FString ModeStr = UEnum::GetValueAsString(Mode);
    if (ModeStr.Contains(TEXT("::")))
    {
        ModeStr.Split(TEXT("::"), nullptr, &ModeStr);
    }
    Batch.BehaviorMode = ModeStr;
    
    Batch.FacialState = NPCActionKeys::Value_Neutral;

    FGameAction Action;
    Action.ActionType = ActionName;
    Action.TargetID = TargetID;

    if (!Content.IsEmpty())
    {
        Action.Parameters.Add(NPCActionKeys::Key_Content, Content);
    }

    if (!ExtraParamsJson.IsEmpty())
    {
        TSharedPtr<FJsonObject> JsonObj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExtraParamsJson);
        // Deserialize and add to params
        if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
        {
            for (auto& Pair : JsonObj->Values)
            {
                Action.Parameters.Add(Pair.Key, Pair.Value->AsString());
            }
        }
    }

    Batch.Actions.Add(Action);

    UE_LOG(LogTemp, Warning, TEXT("[Debug] Manually executing action: %s - %s"), *ModeStr, *ActionName);
    ExecuteActionBatch(Batch);
}

void ASmartNPC::Debug_Test_Social_Dialogue()
{
    Debug_ExecuteAction(ENPCBehaviorMode::Common, TEXT("Dialogue"), TEXT("Player"), TEXT("Hello! This is a debug test."), TEXT(""));
}

void ASmartNPC::Debug_Test_Common_Move()
{
    // Test Move to current location + forward 200 units
    FVector Target = GetActorLocation() + GetActorForwardVector() * 200.0f;
    FString Params = FString::Printf(TEXT("{\"x\": \"%f\", \"y\": \"%f\", \"z\": \"%f\"}"), Target.X, Target.Y, Target.Z);
    Debug_ExecuteAction(ENPCBehaviorMode::Common, TEXT("Move"), TEXT(""), TEXT(""), Params);
}

void ASmartNPC::Debug_Test_Combat_Attack()
{
    Debug_ExecuteAction(ENPCBehaviorMode::Combat, TEXT("Attack"), TEXT("Player"), TEXT(""), TEXT(""));
}

void ASmartNPC::Debug_Test_Orchestra_Pipeline()
{
    // Simulate a JSON message from Cognitive Engine
    // Contains array of batches: One for THIS npc (Social Mode, Happy, Dialogue + Move), one for another.
    FString MockJson = FString::Printf(
        TEXT("["
             "  {"
             "    \"agent_id\": \"%s\","
             "    \"behavior_mode\": \"Social\","
             "    \"facial_state\": \"Happy\","
             "    \"actions\": ["
             "      { \"action_type\": \"Dialogue\", \"text\": \"First, I speak!\", \"emotion\": \"Happy\" },"
             "      { \"action_type\": \"Move\", \"target_id\": \"Point A\", \"speed\": \"500\" },"
             "      { \"action_type\": \"Wait\", \"duration\": \"2.0\" },"
             "      { \"action_type\": \"Move\", \"target_id\": \"Point B\", \"speed\": \"300\" }"
             "    ]"
             "  },"
             "  {"
             "    \"agent_id\": \"Ghost_NPC\","
             "    \"behavior_mode\": \"Combat\","
             "    \"facial_state\": \"Angry\","
             "    \"actions\": ["
             "      { \"action_type\": \"Dialogue\", \"text\": \"I will stop you!\", \"emotion\": \"Angry\" },"
             "      { \"action_type\": \"Attack\", \"target_id\": \"Player\" },"
             "      { \"action_type\": \"Stop\" }" 
             "    ]"
             "  }"
             "]"),
        *AgentID
    );

    UE_LOG(LogTemp, Log, TEXT("[Debug] Testing Orchestra Pipeline with JSON: %s"), *MockJson);

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            // Inject the message directly into HandleMessage (via introspection or public method if available, 
            // but since HandleMessage is private/protected and bound to delegate, we might need a public trigger 
            // OR just call BindSocket logic. 
            // However, for testing, let's assume we can't call private HandleMessage easily without reflection 
            // or making it public. 
            // Let's modify NPCManager to have a public 'Debug_InjectMessage' or similar, 
            // OR just cast/call if it was public (it's private in header).
            
            // Re-checking NPCManager.h... HandleMessage is private UFUNCTION.
            // But we can use FindFunction to call it via ProcessEvent!
            
            UFunction* Func = Manager->FindFunction(TEXT("HandleMessage"));
            if (Func)
            {
                struct FParams
                {
                    FString Msg;
                };
                FParams Params;
                Params.Msg = MockJson;
                Manager->ProcessEvent(Func, &Params);
            }
        }
    }
}


// --- Default C++ Implementations ---
/**
 * Move to a specific location
 * @param TargetLocation: Target location to move to
 * @param MoveType: Type of movement (Walk, Run, Sprint)
 * @param AcceptanceRadius: Radius to accept the move
 */
void ASmartNPC::ExecuteMoveToLocation(FVector TargetLocation, int MoveType, float AcceptanceRadius)
{
    // Move
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        // Simple Move to Location
        AI->MoveToLocation(TargetLocation, AcceptanceRadius);
    }
}

/**
 * Keep distance from a target actor
 * @param TargetActor: Target actor to keep distance from
 * @param Distance: Distance to keep from the target actor
 * @param Speed: Speed to move at
 */
void ASmartNPC::ExecuteKeepDistance(AActor* TargetActor, float Distance, float Speed)
{
    if (!TargetActor) return;

    // Apply Speed
    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->MaxWalkSpeed = (Speed > 0) ? Speed : CurrentStats.Movement.RunSpeed;
    }

    // Calculate Target Position
    FVector Direction = GetActorLocation() - TargetActor->GetActorLocation();
    Direction.Normalize();
    FVector TargetPos = TargetActor->GetActorLocation() + Direction * Distance;

    // Move
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->MoveToLocation(TargetPos, 50.f); 
    }
}

/**
 * Wait for a specific duration
 * @param Duration: Duration to wait
 */
void ASmartNPC::ExecuteWait(float Duration)
{
    // Just stop movement
    if (AController* C = GetController())
    {
        C->StopMovement();
    }
    
    // Note: The actual "Wait" delay is usually handled by the Behavior Tree Task.
    // This event is for any immediate visual reaction associated with waiting.
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s : Waiting for %.1f seconds"), *AgentID, Duration);
}

/**
 * Execute dialogue
 * @param DialogueText: Dialogue text to display
 * @param EmotionID: Emotion ID to use
 */
void ASmartNPC::ExecuteDialogue(const FString& DialogueText, const FString& EmotionID)
{
    // Log dialogue. In a real game, this would spawn a widget or play sound.
    UE_LOG(LogTemp, Log, TEXT("[Dialogue] %s (%s): \"%s\""), *AgentID, *EmotionID, *DialogueText);
}

/**
 * Face a specific location
 * @param TargetLocation: Location to face
 * @param TurnSpeed: Speed to turn at
 */
void ASmartNPC::ExecuteFaceRotate(FVector TargetLocation, float TurnSpeed)
{
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->SetFocalPoint(TargetLocation);
        UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s: Start Facing %s (Delegate to BT Task or AIController)"), *AgentID, *TargetLocation.ToString());
    }
}

/**
 * Perform attack
 * @param TargetActor: Target actor to attack
 * @param AttackType: Attack type to use
 */
void ASmartNPC::ExecutePerformAttack(AActor* TargetActor, const FString& AttackType)
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Attacks %s with %s"), *AgentID, TargetActor ? *TargetActor->GetName() : NPCActionKeys::Value_None, *AttackType);

    // Stop movement to attack
    if (AController* C = GetController())
    {
        C->StopMovement();
    }

    // Turn towards target
    if (TargetActor)
    {
        if (AAIController* AI = Cast<AAIController>(GetController()))
        {
            AI->SetFocus(TargetActor);
        }
    }
}

/**
 * Defend
 * @param bStartDefend: Start or stop defending
 */
void ASmartNPC::ExecuteDefend(bool bStartDefend)
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Defend: %s"), *AgentID, bStartDefend ? TEXT("START") : TEXT("END"));
}

/**
 * Roll
 */
void ASmartNPC::ExecuteRoll()
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Rolls"), *AgentID);
}

/**
 * Perform hand signal
 * @param SignalName: Name of the hand signal to perform
 */
void ASmartNPC::ExecuteHandSignal(const FString& SignalName)
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Hand Signal: %s"), *AgentID, *SignalName);
}

/**
 * Perform emote
 * @param EmoteName: Name of the emote to perform
 */
void ASmartNPC::ExecuteEmote(const FString& EmoteName)
{
     UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Emote: %s"), *AgentID, *EmoteName);
}

// --- Action Queue System ---

/**
 * Stop all actions
 */
void ASmartNPC::StopAllActions()
{
    // 1. Clear Queue
    ActionQueue.Empty();
    bIsBusy = false;
    CurrentActionID = NPCActionKeys::Value_None;

    // 2. Clear Blackboard
    if (ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(GetController()))
    {
        if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
        {
            BB->SetValueAsString(ASmartNPCAIController::Key_SubAction, TEXT("Idle"));
            BB->ClearValue(ASmartNPCAIController::Key_TargetLocation);
            BB->ClearValue(ASmartNPCAIController::Key_TargetActor);
        }
        AI->StopMovement();
    }
    
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s: Stopped All Actions and Cleared Queue."), *AgentID);
}

/**
 * Process next action in the queue
 */
void ASmartNPC::ProcessNextAction()
{
    if (bIsBusy) return;
    if (ActionQueue.IsEmpty()) return;

    FGameAction Action;
    if (ActionQueue.Dequeue(Action))
    {
        bIsBusy = true;
        CurrentActionID = Action.ActionType;
        
        ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(GetController());
        if (!AI) 
        {
            // No controller? Finish immediately.
            OnActionCompleted();
            return;
        }

        UBlackboardComponent* BB = AI->GetBlackboardComponent();
        if (!BB) return;

        // Set Blackboard Keys to trigger Behavior Tree
        BB->SetValueAsString(ASmartNPCAIController::Key_SubAction, Action.ActionType);

        TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
        for (const auto& Pair : Action.Parameters)
        {
            JsonObj->SetStringField(Pair.Key, Pair.Value);
        }
        if (!Action.TargetID.IsEmpty())
        {
            JsonObj->SetStringField(NPCActionKeys::Key_TargetID, Action.TargetID);
        }

        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
        BB->SetValueAsString(ASmartNPCAIController::Key_ActionParameters, OutputString);

        // Handle Target Location
        if (Action.Parameters.Contains(NPCActionKeys::Loc_X) && 
            Action.Parameters.Contains(NPCActionKeys::Loc_Y) && 
            Action.Parameters.Contains(NPCActionKeys::Loc_Z))
        {
             FVector Loc;
             Loc.X = FCString::Atof(*Action.Parameters[NPCActionKeys::Loc_X]);
             Loc.Y = FCString::Atof(*Action.Parameters[NPCActionKeys::Loc_Y]);
             Loc.Z = FCString::Atof(*Action.Parameters[NPCActionKeys::Loc_Z]);
             BB->SetValueAsVector(ASmartNPCAIController::Key_TargetLocation, Loc);
        }

        UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s: Starting Action %s (Queue Remaining: %d)"), *AgentID, *Action.ActionType, ActionQueue.IsEmpty() ? 0 : 1);
        
        // Timeout or failsafe? 
        // For now, relies on Behavior Tree calling OnActionCompleted via Task
    }
}

/**
 * Called when an action is completed
 */
void ASmartNPC::OnActionCompleted()
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s: Action %s Completed."), *AgentID, *CurrentActionID);
    bIsBusy = false;
    CurrentActionID = NPCActionKeys::Value_None;
    
    // Process next item in queue
    ProcessNextAction();
}

