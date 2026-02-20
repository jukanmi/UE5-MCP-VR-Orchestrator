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
#include "../Component/NPCInventoryComponent.h" // Include Inventory
#include "NPCInteractionDataAsset.h"

ASmartNPC::ASmartNPC()
{
    PrimaryActorTick.bCanEverTick = true;
    AgentID = TEXT("UnknownAgent");
    AIControllerClass = ASmartNPCAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    // Create Inventory Component
    InventoryComponent = CreateDefaultSubobject<UNPCInventoryComponent>(TEXT("InventoryComponent"));
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
/**
 * 추후 스크립트로 옮길예정
 */
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
/**
 * 추후 스크립트로 옮길예정
 */
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

    // Behavior Mode: 문자열 → Enum 변환
    // 왜 Scoped 이름을 먼저 시도하는가: BlueprintType UENUM은 짧은 이름("Common")을
    // 인식 못 하는 경우가 있어 항상 "ENPCBehaviorMode::Common" 형식으로 먼저 시도
    ENPCBehaviorMode Mode = ENPCBehaviorMode::None;
    const UEnum* ModeEnum = StaticEnum<ENPCBehaviorMode>();
    if (ModeEnum)
    {
        FString ScopedName = FString::Printf(TEXT("ENPCBehaviorMode::%s"), *Batch.BehaviorMode);
        int64 Val = ModeEnum->GetValueByName(FName(*ScopedName));
        if (Val == INDEX_NONE)
        {
            // Fallback: 짧은 이름 시도 (레거시 호환)
            Val = ModeEnum->GetValueByName(FName(*Batch.BehaviorMode));
        }
        
        if (Val != INDEX_NONE)
        {
            Mode = (ENPCBehaviorMode)Val;
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SmartNPC] '%s' is not a valid BehaviorMode, defaulting to None"),
                *Batch.BehaviorMode);
        }
    }
    BB->SetValueAsEnum(ASmartNPCAIController::Key_BehaviorMode, (uint8)Mode);

    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] BehaviorMode set: '%s' → Enum(%d)"),
        *Batch.BehaviorMode, (int32)Mode);

    // Facial State: 문자열 → Enum 변환 (같은 패턴)
    EFacialState Facial = EFacialState::Neutral;
    const UEnum* FacialEnum = StaticEnum<EFacialState>();
    if (FacialEnum)
    {
        FString ScopedName = FString::Printf(TEXT("EFacialState::%s"), *Batch.FacialState);
        int64 Val = FacialEnum->GetValueByName(FName(*ScopedName));
        if (Val == INDEX_NONE)
        {
            Val = FacialEnum->GetValueByName(FName(*Batch.FacialState));
        }

        if (Val != INDEX_NONE)
        {
            Facial = (EFacialState)Val;
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SmartNPC] '%s' is not a valid FacialState, defaulting to Neutral"),
                *Batch.FacialState);
        }
    }
    BB->SetValueAsEnum(ASmartNPCAIController::Key_FacialState, (uint8)Facial);
    
    // Sync Local State
    CurrentFacialState = Facial;
}
// 검토 필요: Action Dispatching 로직 (병렬 vs 큐잉) 및 개별 액션 처리 방식
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

float ASmartNPC::ParseMoveSpeed(const EMoveType& Type) const
{
    if (Type == EMoveType::Walk) return CurrentStats.Movement.WalkSpeed;
    if (Type == EMoveType::Run) return CurrentStats.Movement.RunSpeed;
    if (Type == EMoveType::Sprint) return CurrentStats.Movement.SprintSpeed;
    if (Type == EMoveType::Crouch) return CurrentStats.Movement.CrouchSpeed;
    return CurrentStats.Movement.WalkSpeed; // Default
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

        // target_loc JSON 오브젝트를 파싱하여 Blackboard FVector로 변환
        if (Action.Parameters.Contains(NPCActionKeys::Key_TargetLoc))
        {
            FString LocJsonStr = Action.Parameters[NPCActionKeys::Key_TargetLoc];
            TSharedPtr<FJsonObject> LocJson;
            TSharedRef<TJsonReader<>> LocReader = TJsonReaderFactory<>::Create(LocJsonStr);
            
            if (FJsonSerializer::Deserialize(LocReader, LocJson) && LocJson.IsValid())
            {
                FVector Loc;
                Loc.X = LocJson->GetNumberField(NPCActionKeys::Loc_X);
                Loc.Y = LocJson->GetNumberField(NPCActionKeys::Loc_Y);
                Loc.Z = LocJson->GetNumberField(NPCActionKeys::Loc_Z);
                BB->SetValueAsVector(ASmartNPCAIController::Key_TargetLocation, Loc);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Failed to parse target_loc JSON: %s"), *LocJsonStr);
            }
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

// --- Default C++ Implementations ---
/**
 * Move to a specific location
 * @param TargetLocation: Target location to move to
 * @param MoveType: Type of movement (Walk, Run, Sprint)
 * @param AcceptanceRadius: Radius to accept the move
 */
void ASmartNPC::ExecuteMoveToLocation(FVector TargetLocation, EMoveType SpeedType, float AcceptanceRadius)
{
    // Apply Speed based on MoveType
    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
        Movement->MaxWalkSpeed = ParseMoveSpeed(SpeedType);


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
 * @param SpeedType: Speed type to move at (Walk, Run, Sprint, Crouch)
 */
void ASmartNPC::ExecuteKeepDistance(AActor* TargetActor, EMoveType SpeedType, float Distance)
{
    if (!TargetActor) return;

    // Apply Speed based on MoveType
    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
        Movement->MaxWalkSpeed = ParseMoveSpeed(SpeedType);

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
    //ToDo: Implement Wait use BT
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s : Waiting for %.1f seconds"), *AgentID, Duration);
}

/**
 * Execute dialogue
 * @param DialogueText: Dialogue text to display
 * @param EmotionID: Emotion ID to use
 */
void ASmartNPC::ExecuteDialogue(const FString& DialogueText, const EFacialState& EmotionID)
{
    // Log dialogue. In a real game, this would spawn a widget or play sound.
    //Todo: Implement Dialogue (Widget, Sound, etc.), EFacialState에 따른 표정 변화도 적용
    UE_LOG(LogTemp, Log, TEXT("[Dialogue] %s (%s): \"%s\""), *AgentID, *GetEnumValueAsString(EmotionID), *DialogueText);
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
 * @param AttackType: Attack type to use
 */
void ASmartNPC::ExecutePerformAttack(const FString& AttackType)
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Attacks with %s"), *AgentID, *AttackType);

    // Stop movement to attack
    if (AController* C = GetController())
    {
        C->StopMovement();
    }
    //Todo: Implement Attack by AttackType (Damage, Animation, etc.)
}

/**
 * Defend
 * @param bStartDefend: Start or stop defending
 */
void ASmartNPC::ExecuteDefend(bool bStartDefend)
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Defend: %s"), *AgentID, bStartDefend ? TEXT("START") : TEXT("END"));
    //Todo: Implement Defend (Animation, Invincibility Frames, etc.)
}

/**
 * Dodge
 */
void ASmartNPC::ExecuteDodge()
{
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] %s Dodges"), *AgentID);
    //Todo: Implement Dodge (Animation, Invincibility Frames, etc.)
}
//legacy
void ASmartNPC::ExecuteGenericAction(const FString& ActionType, const FString& TargetID, const FString& Content, const FString& ExtraParams)
{
    // Redirect generic actions to new interaction system if possible
    ExecuteInteraction(ActionType, nullptr, TargetID, ExtraParams);
}

void ASmartNPC::ExecuteInteraction(const FString& InteractionType, AActor* TargetActor, const FString& TargetID, const FString& ExtraParams)
{
    // Guard against empty or invalid interaction types to prevent log spam
    if (InteractionType.IsEmpty()) return;

    // Ignore 'Idle' or 'None' types for now
    if (InteractionType.Equals(TEXT("Idle"), ESearchCase::IgnoreCase) || 
        InteractionType.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] ExecuteInteraction: %s (Target: %s, ID: %s)"), *InteractionType, TargetActor ? *TargetActor->GetName() : TEXT("None"), *TargetID);

    // 1. Body State & Movement
    if (InteractionType.Equals(NPCActionKeys::Interact_Sit, ESearchCase::IgnoreCase))
    {
        ExecuteSit(TargetActor);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_LieDown, ESearchCase::IgnoreCase))
    {
        ExecuteLieDown(TargetActor);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_StandUp, ESearchCase::IgnoreCase))
    {
        ExecuteStandUp();
    }
    
    // 2. Inventory (Requires Inventory Component)
    else if (InteractionType.Equals(NPCActionKeys::Interact_PickUp, ESearchCase::IgnoreCase))
    {
        ExecutePickUp(TargetActor);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Drop, ESearchCase::IgnoreCase))
    {
        ExecuteDropItem(TargetID);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Eat, ESearchCase::IgnoreCase))
    {
        ExecuteEat(TargetID);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Wear, ESearchCase::IgnoreCase))
    {
        ExecuteWear(TargetID);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Unequip, ESearchCase::IgnoreCase))
    {
        ExecuteUnequip(TargetID);
    }

    // 3. Task
    else if (InteractionType.Equals(NPCActionKeys::Interact_Clean, ESearchCase::IgnoreCase))
    {
        ExecuteClean(TargetActor);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Repair, ESearchCase::IgnoreCase))
    {
        ExecuteRepair(TargetActor);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Read, ESearchCase::IgnoreCase))
    {
        ExecuteRead(TargetActor);
    }

    // 4. Performance
    else if (InteractionType.Equals(NPCActionKeys::Interact_Pray, ESearchCase::IgnoreCase))
    {
        ExecutePray();
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Dance, ESearchCase::IgnoreCase))
    {
        // Style can be passed in ExtraParams or appended to Key
        ExecuteDance(ExtraParams); 
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Sing, ESearchCase::IgnoreCase))
    {
        ExecuteSing(ExtraParams);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_HandSignal, ESearchCase::IgnoreCase))
    {
        ExecuteHandSignal(ExtraParams);
    }
    else if (InteractionType.Equals(NPCActionKeys::Interact_Emote, ESearchCase::IgnoreCase))
    {
        ExecuteEmote(ExtraParams);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Unknown Interaction Type: %s"), *InteractionType);
    }
}

// --- Protected Helpers ---

void ASmartNPC::PlayInteractionMontage(const FString& Key)
{
    if (!InteractionData)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] InteractionData Asset is NOT set in Blueprint!"));
        return;
    }

    UAnimMontage* Montage = InteractionData->FindMontage(Key);
    if (Montage)
    {
        float Duration = PlayAnimMontage(Montage);
        UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Playing Montage for Key: %s (Duration: %.2f)"), *Key, Duration);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] No Montage found for Key: %s"), *Key);
    }
}

void ASmartNPC::ExecuteSit(AActor* TargetSeat)
{
    // Constraint 2: Just play animation for now. No complex snapping.
    PlayInteractionMontage(NPCActionKeys::Interact_Sit);
}

void ASmartNPC::ExecuteLieDown(AActor* TargetBed)
{
    // Constraint 2: Just play animation.
    PlayInteractionMontage(NPCActionKeys::Interact_LieDown);
}

void ASmartNPC::ExecuteStandUp()
{
    PlayInteractionMontage(NPCActionKeys::Interact_StandUp);
}

void ASmartNPC::ExecutePickUp(AActor* TargetItem)
{
    // ToDo: Need a robust way to get UItemDataAsset from TargetActor.
    // For now, destroy actor to simulate pickup if simple.
    PlayInteractionMontage(NPCActionKeys::Interact_PickUp);
    
    if (TargetItem)
    {
        UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Picked up %s (Visual only - Logic requires Item Interface)"), *TargetItem->GetName());
        TargetItem->Destroy();
    }
}

void ASmartNPC::ExecuteDropItem(const FString& ItemID)
{
    if (InventoryComponent)
    {
        if (InventoryComponent->RemoveItem(ItemID, 1))
        {
             PlayInteractionMontage(NPCActionKeys::Interact_Drop);
             // ToDo: Spawn actor in world?
             UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Dropped Item: %s"), *ItemID);
        }
        else
        {
             UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Failed to drop item (Not found): %s"), *ItemID);
        }
    }
}

void ASmartNPC::ExecuteEat(const FString& ItemID)
{
    if (InventoryComponent)
    {
        // Check if exists logic
        // For 'Eat', we usually consume it.
        if (InventoryComponent->RemoveItem(ItemID, 1))
        {
             PlayInteractionMontage(NPCActionKeys::Interact_Eat);
             // Restore Health/Hunger logic here
             UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Ate Item: %s"), *ItemID);
        }
        else
        {
             UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] No item to eat: %s"), *ItemID);
        }
    }
}

void ASmartNPC::ExecuteWear(const FString& ItemID)
{
     if (InventoryComponent && InventoryComponent->HasItem(ItemID))
     {
         PlayInteractionMontage(NPCActionKeys::Interact_Wear);
         UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Equipped: %s"), *ItemID);
         // Attach mesh logic...
     }
}

void ASmartNPC::ExecuteUnequip(const FString& ItemID)
{
     PlayInteractionMontage(NPCActionKeys::Interact_Unequip);
     UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Unequipped: %s"), *ItemID);
}

void ASmartNPC::ExecuteClean(AActor* TargetZone)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Clean);
}

void ASmartNPC::ExecuteRepair(AActor* TargetObject)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Repair);
}

void ASmartNPC::ExecuteRead(AActor* TargetBook)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Read);
}

void ASmartNPC::ExecutePray()
{
    PlayInteractionMontage(NPCActionKeys::Interact_Pray);
}

void ASmartNPC::ExecuteDance(const FString& Style)
{
    // Key might be "Dance" or "Dance_Salsa"
    FString Key = NPCActionKeys::Interact_Dance;
    if (!Style.IsEmpty())
    {
        Key = Key + TEXT("_") + Style;
    }
    PlayInteractionMontage(Key);
}

void ASmartNPC::ExecuteSing(const FString& SongName)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Sing);
    // Trigger Sound Cue...
}

void ASmartNPC::ExecuteHandSignal(const FString& SignalName)
{
    // If SignalName is provided, use it as key, or append?
    // Usually HandSignal is the category.
    // Let's assume SignalName IS the key if valid.
    if (!SignalName.IsEmpty())
    {
         PlayInteractionMontage(SignalName);
    }
    else
    {
         // Default generic wave?
         PlayInteractionMontage(TEXT("HandSignal_Wave"));
    }
}

void ASmartNPC::ExecuteEmote(const FString& EmoteName)
{
    if (!EmoteName.IsEmpty())
    {
        PlayInteractionMontage(EmoteName);
    }
}

void ASmartNPC::Debug_Test_Interaction(FString InteractionKey, FString ExtraParams)
{
    // [Test] 상호작용 시스템 테스트 (Deprecated Interaction -> Generic Action)
    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Debug_Test_Interaction: Key=%s, Extra=%s"), *InteractionKey, *ExtraParams);
    
    // InteractionKey가 곧 ActionType이 됨 (Dance, Clean 등)
    // TargetObject 없이 즉시 상호작용 실행
    ExecuteInteraction(InteractionKey, nullptr, TEXT("DebugTarget"), ExtraParams);
}


// --- Debug Functions ---

void ASmartNPC::Debug_ExecuteAction(ENPCBehaviorMode Mode, FString ActionName, FString TargetID, FString Content, FString ExtraParamsJson)
{
    // [Goal] 수동 액션 실행: 입력된 파라미터를 기반으로 ActionBatch를 생성하여 즉시 실행
    // Inputs:
    // - Mode: 행동 모드 (Combat, Social 등) - Enum으로 입력받아 오타 방지
    // - ActionName: 수행할 액션 이름 (Move, Dialogue 등)
    // - TargetID: 대상 ID ("Player") 또는 좌표 문자열 ("100,200,300")
    // - Content: 대화 내용 또는 액션 상세 내용
    // - ExtraParamsJson: 추가 파라미터 JSON 문자열 (예: {"speed": "300"})
    //
    // Outputs:
    // - Blackboard 상태 변경 (BehaviorMode, FacialState)
    // - ActionQueue에 액션 추가 및 실행
    // - Log 출력

    FActionBatch Batch;
    Batch.AgentID = AgentID;
    
    // 1. Behavior Mode 변환 (Enum -> String)
    // UEnum::GetEnumName은 접두사를 포함하지 않는 순수 이름을 반환할 수 있음 (버전에 따라 다름)
    // 안전하게 GetValueAsString 후 파싱: "ENPCBehaviorMode::Common" -> "Common"
    FString ModeStr = UEnum::GetValueAsString(Mode);
    if (ModeStr.Contains(TEXT("::")))
    {
        ModeStr.Split(TEXT("::"), nullptr, &ModeStr);
    }
    Batch.BehaviorMode = ModeStr;
    Batch.FacialState = NPCActionKeys::Value_Neutral; // Debug 기본값

    FGameAction Action;
    Action.ActionType = ActionName;
    
    // 2. TargetID 처리: 좌표 벡터 자동 감지
    // Move 액션의 경우 TargetID에 좌표("X,Y,Z")가 들어오면 JSON 파라미터로 변환하여 처리
    if (ActionName.Contains(NPCActionKeys::Action_Move, ESearchCase::IgnoreCase))
    {
        FVector TargetLoc;
        if (TargetLoc.InitFromString(TargetID))
        {
            TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
            LocObj->SetNumberField(NPCActionKeys::Loc_X, TargetLoc.X);
            LocObj->SetNumberField(NPCActionKeys::Loc_Y, TargetLoc.Y);
            LocObj->SetNumberField(NPCActionKeys::Loc_Z, TargetLoc.Z);

            FString LocJsonString;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&LocJsonString);
            FJsonSerializer::Serialize(LocObj.ToSharedRef(), Writer);

            Action.Parameters.Add(NPCActionKeys::Key_TargetLoc, LocJsonString);
            Action.TargetID = TEXT(""); // 좌표 이동이므로 ID는 비움 (MoveToLocation 사용)
            
            UE_LOG(LogTemp, Log, TEXT("[Debug] Parsed Vector Target: %s"), *TargetLoc.ToString());
        }
        else
        {
            Action.TargetID = TargetID; // 일반 대상 ID (Actor 추적)
        }
    }
    else
    {
        Action.TargetID = TargetID;
    }


    if (!ExtraParamsJson.IsEmpty())
    {
        TSharedPtr<FJsonObject> JsonObj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExtraParamsJson);
        if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
        {
            for (auto& Pair : JsonObj->Values)
            {
                Action.Parameters.Add(Pair.Key, Pair.Value->AsString());
            }
        }
    }

    Batch.Actions.Add(Action);

    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Debug Execute: Mode=%s, Action=%s, Params=%d"), 
        *Batch.BehaviorMode, *ActionName, Action.Parameters.Num());
        
    ExecuteActionBatch(Batch);
}

void ASmartNPC::Debug_Test_Social_Dialogue()
{
    Debug_ExecuteAction(ENPCBehaviorMode::Social, NPCActionKeys::Action_Dialogue, TEXT("Player"), TEXT("Hello! This is a debug test."), TEXT(""));
}

void ASmartNPC::Debug_Test_Common_Move()
{
    FVector Target = GetActorLocation() + GetActorForwardVector() * 200.0f;
    Debug_ExecuteAction(ENPCBehaviorMode::Common, NPCActionKeys::Action_Move, Target.ToString(), TEXT(""), TEXT(""));
}

void ASmartNPC::Debug_Test_Combat_Attack()
{
    Debug_ExecuteAction(ENPCBehaviorMode::Combat, NPCActionKeys::Action_Attack, TEXT("Player"), TEXT(""), TEXT(""));
}

void ASmartNPC::Debug_Test_Orchestra_Pipeline()
{
    // [Test] Cognitive Engine의 전체 파이프라인 시뮬레이션
    // 복잡한 JSON 메시지를 생성하여 NPCManager에 주입 -> SmartNPC::HandleBroadcastMessage 트리거
    
    // Output:
    // - AgentID가 일치하는 NPC는 Social 모드로 변경되고 대화 -> 대기 -> 이동 시퀀스 수행
    // - "Ghost_NPC" 메시지는 무시됨

    TArray<TSharedPtr<FJsonValue>> AgentList;

    // 1. 내 에이전트 (Social 모드, 대화 + 이동)
    TSharedPtr<FJsonObject> MyAgent = MakeShareable(new FJsonObject);
    MyAgent->SetStringField("agent_id", AgentID);
    MyAgent->SetStringField("behavior_mode", NPCActionKeys::Mode_Social);
    MyAgent->SetStringField("facial_state", "Happy");

    TArray<TSharedPtr<FJsonValue>> MyActions;
    
    // Action 1: Dialogue
    TSharedPtr<FJsonObject> Act1 = MakeShareable(new FJsonObject);
    Act1->SetStringField("action_type", NPCActionKeys::Action_Dialogue);
    Act1->SetStringField("text", "Pipeline Test: Hello!");
    Act1->SetStringField("emotion", "Happy");
    MyActions.Add(MakeShareable(new FJsonValueObject(Act1)));

    // Action 2: Wait
    TSharedPtr<FJsonObject> Act2 = MakeShareable(new FJsonObject);
    Act2->SetStringField("action_type", NPCActionKeys::Action_Wait);
    Act2->SetStringField("duration", "1.5");
    MyActions.Add(MakeShareable(new FJsonValueObject(Act2)));

    MyAgent->SetArrayField("actions", MyActions);
    AgentList.Add(MakeShareable(new FJsonValueObject(MyAgent)));


    // 2. 다른 에이전트 (Ghost_NPC) - 무시되어야 함
    TSharedPtr<FJsonObject> GhostAgent = MakeShareable(new FJsonObject);
    GhostAgent->SetStringField("agent_id", "Ghost_NPC");
    GhostAgent->SetStringField("behavior_mode", NPCActionKeys::Mode_Combat);
    GhostAgent->SetArrayField("actions", TArray<TSharedPtr<FJsonValue>>());
    AgentList.Add(MakeShareable(new FJsonValueObject(GhostAgent)));


    // Serialize to String
    FString MockJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&MockJson);
    FJsonSerializer::Serialize(AgentList, Writer);

    UE_LOG(LogTemp, Log, TEXT("[Debug] Testing Orchestra Pipeline with JSON: %s"), *MockJson);

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            UFunction* Func = Manager->FindFunction(TEXT("HandleMessage"));
            if (Func)
            {
                struct FParams { FString Msg; };
                FParams Params;
                Params.Msg = MockJson;
                Manager->ProcessEvent(Func, &Params);
            }
        }
    }
}