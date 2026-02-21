#include "NPCActionComponent.h"
#include "NPCStateComponent.h"
#include "NPCInventoryComponent.h"
#include "../AI/SmartNPCAIController.h"
#include "../AI/NPCActionKeys.h"
#include "../AI/NPCInteractionDataAsset.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

UNPCActionComponent::UNPCActionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNPCActionComponent::BeginPlay()
{
    Super::BeginPlay();

    // Owner에서 필요한 컴포넌트들을 자동 검색
    if (AActor* Owner = GetOwner())
    {
        StateComponent = Owner->FindComponentByClass<UNPCStateComponent>();
        InventoryComponent = Owner->FindComponentByClass<UNPCInventoryComponent>();

        if (!StateComponent)
        {
            UE_LOG(LogTemp, Warning, TEXT("[NPCAction] Owner '%s' has no NPCStateComponent!"), *Owner->GetName());
        }
    }
}

// === Helper Functions ===

ASmartNPCAIController* UNPCActionComponent::GetOwnerAIController() const
{
    if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
    {
        return Cast<ASmartNPCAIController>(OwnerPawn->GetController());
    }
    return nullptr;
}

FString UNPCActionComponent::GetOwnerAgentID() const
{
    // SmartNPC의 AgentID에 접근하기 위한 헬퍼
    // 왜 직접 캐스트하는가: AgentID는 SmartNPC 고유의 식별자이므로
    if (AActor* Owner = GetOwner())
    {
        // Reflection으로 AgentID 프로퍼티 접근 (SmartNPC에 직접 의존하지 않기 위해)
        if (FProperty* Prop = Owner->GetClass()->FindPropertyByName(TEXT("AgentID")))
        {
            FString Result;
            if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
            {
                Result = StrProp->GetPropertyValue_InContainer(Owner);
                return Result;
            }
        }
        return Owner->GetName();
    }
    return TEXT("Unknown");
}

float UNPCActionComponent::ParseMoveSpeed(const EMoveType& Type) const
{
    if (!StateComponent) return 200.f; // Fallback

    if (Type == EMoveType::Walk)   return StateComponent->CurrentStats.Movement.WalkSpeed;
    if (Type == EMoveType::Run)    return StateComponent->CurrentStats.Movement.RunSpeed;
    if (Type == EMoveType::Sprint) return StateComponent->CurrentStats.Movement.SprintSpeed;
    if (Type == EMoveType::Crouch) return StateComponent->CurrentStats.Movement.CrouchSpeed;
    return StateComponent->CurrentStats.Movement.WalkSpeed;
}

// === Action Batch System ===


void UNPCActionComponent::ExecuteActionBatch(const FActionBatch& Batch)
{
    // 1. Actions 분배 (Dialogue = 즉시, 나머지 = Queue)
    // 개별 액션의 BehaviorMode와 FacialState는 Dispatch/Queue 처리 시 업데이트됩니다.
    DispatchActions(Batch.Actions);

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - Batch Executed. Actions: %d"),
        *GetOwnerAgentID(), Batch.Actions.Num());
}

void UNPCActionComponent::UpdateActionState(const FGameAction& Action)
{
    ASmartNPCAIController* AI = GetOwnerAIController();
    if (!AI) return;

    UBlackboardComponent* BB = AI->GetBlackboardComponent();
    if (!BB) return;

    // 1. BehaviorMode 업데이트 (문자열 → Enum)
    if (!Action.BehaviorMode.IsEmpty())
    {
        const UEnum* ModeEnum = StaticEnum<ENPCBehaviorMode>();
        if (ModeEnum)
        {
            int64 Value = ModeEnum->GetValueByNameString(Action.BehaviorMode);
            if (Value == INDEX_NONE) Value = ModeEnum->GetValueByName(FName(*Action.BehaviorMode));
            
            if (Value == INDEX_NONE)
            {
                FString FullName = FString::Printf(TEXT("ENPCBehaviorMode::%s"), *Action.BehaviorMode);
                Value = ModeEnum->GetValueByName(FName(*FullName));
            }

            if (Value != INDEX_NONE)
            {
                BB->SetValueAsEnum(ASmartNPCAIController::Key_BehaviorMode, (uint8)Value);
            }
        }
    }

    // 2. FacialState 업데이트 (문자열 → Enum)
    if (!Action.FacialState.IsEmpty())
    {
        const UEnum* FacialEnum = StaticEnum<EFacialState>();
        if (FacialEnum)
        {
            int64 Value = FacialEnum->GetValueByNameString(Action.FacialState);
            if (Value == INDEX_NONE) Value = FacialEnum->GetValueByName(FName(*Action.FacialState));
            
            if (Value == INDEX_NONE)
            {
                FString FullName = FString::Printf(TEXT("EFacialState::%s"), *Action.FacialState);
                Value = FacialEnum->GetValueByName(FName(*FullName));
            }

            if (Value != INDEX_NONE)
            {
                if (StateComponent)
                {
                    StateComponent->SetFacialExpression((EFacialState)Value);
                }
            }
        }
    }
}

void UNPCActionComponent::DispatchActions(const TArray<FGameAction>& Actions)
{
    for (const FGameAction& Action : Actions)
    {
        // 1. Critical Stop
        if (Action.ActionType.Equals(NPCActionKeys::Action_Stop, ESearchCase::IgnoreCase))
        {
            StopAllActions();
            continue;
        }

        // 2. Parallel Action: Dialogue (즉시 실행, 큐잉하지 않음)
        if (Action.ActionType.Equals(NPCActionKeys::Action_Dialogue, ESearchCase::IgnoreCase))
        {
            // 대화는 즉시 감정/상태 변화를 동반할 수 있음
            UpdateActionState(Action);

            FString TextContent = Action.Parameters.FindRef(NPCActionKeys::Key_Text);
            FString Emotion = Action.Parameters.FindRef(NPCActionKeys::Key_Emotion);
            if (Emotion.IsEmpty()) Emotion = NPCActionKeys::Value_Neutral;

            // Emotion 문자열 → EFacialState 변환
            EFacialState EmotionEnum = EFacialState::Neutral;
            const UEnum* FacialEnum = StaticEnum<EFacialState>();
            if (FacialEnum)
            {
                int64 Val = FacialEnum->GetValueByNameString(Emotion);
                if (Val != INDEX_NONE) EmotionEnum = (EFacialState)Val;
            }

            ExecuteDialogue(TextContent, EmotionEnum);
        }
        else
        {
            // 3. Physical Action → Queue에 추가
            ActionQueue.Enqueue(Action);
        }
    }

    // Queue 처리 시작
    ProcessNextAction();
}

// === Action Queue System ===

void UNPCActionComponent::StopAllActions()
{
    ActionQueue.Empty();
    bIsBusy = false;

    if (StateComponent) StateComponent->CurrentActionID = NPCActionKeys::Value_None;

    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
        {
            BB->SetValueAsString(ASmartNPCAIController::Key_SubAction, TEXT("Idle"));
            BB->ClearValue(ASmartNPCAIController::Key_TargetLocation);
            BB->ClearValue(ASmartNPCAIController::Key_TargetActor);
        }
        AI->StopMovement();
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Stopped All Actions."), *GetOwnerAgentID());
}

void UNPCActionComponent::ProcessNextAction()
{
    if (bIsBusy) return;
    if (ActionQueue.IsEmpty()) return;

    FGameAction Action;
    if (ActionQueue.Dequeue(Action))
    {
        bIsBusy = true;
        if (StateComponent) StateComponent->CurrentActionID = Action.ActionType;
        
        // 물리적 액션 시작 전 상태(Mode, Facial) 업데이트
        UpdateActionState(Action);

        ASmartNPCAIController* AI = GetOwnerAIController();
        if (!AI)
        {
            OnActionCompleted();
            return;
        }

        UBlackboardComponent* BB = AI->GetBlackboardComponent();
        if (!BB) return;

        // Blackboard에 액션 정보 설정 (BT Task가 읽어서 실행)
        BB->SetValueAsString(ASmartNPCAIController::Key_SubAction, Action.ActionType);

        // Parameters → JSON 문자열로 변환하여 Blackboard에 저장
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

        // target_loc JSON → FVector 변환
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
                UE_LOG(LogTemp, Warning, TEXT("[NPCAction] Failed to parse target_loc: %s"), *LocJsonStr);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Starting Action '%s'"), *GetOwnerAgentID(), *Action.ActionType);
    }
}

void UNPCActionComponent::OnActionCompleted()
{
    FString CompletedAction = StateComponent ? StateComponent->CurrentActionID : TEXT("Unknown");
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Action '%s' Completed."), *GetOwnerAgentID(), *CompletedAction);

    bIsBusy = false;
    if (StateComponent) StateComponent->CurrentActionID = NPCActionKeys::Value_None;

    // 다음 큐 항목 처리
    ProcessNextAction();
}

void UNPCActionComponent::AbortCurrentAction()
{
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: ABORTING Action"), *GetOwnerAgentID());
    if (StateComponent) StateComponent->CurrentActionID = TEXT("");

    // 물리 상태 초기화 (애니메이션 중지, 이동 중지)
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
    {
        OwnerChar->StopAnimMontage();
        if (AController* C = OwnerChar->GetController())
        {
            C->StopMovement();
        }
    }
}

// === Execute Functions ===

void UNPCActionComponent::ExecuteMoveToLocation(FVector TargetLocation, EMoveType SpeedType, float AcceptanceRadius)
{
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
            Movement->MaxWalkSpeed = ParseMoveSpeed(SpeedType);

        if (AAIController* AI = Cast<AAIController>(OwnerChar->GetController()))
        {
            AI->MoveToLocation(TargetLocation, AcceptanceRadius);
        }
    }
}

void UNPCActionComponent::ExecuteKeepDistance(AActor* TargetActor, EMoveType SpeedType, float Distance)
{
    if (!TargetActor) return;
    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (!OwnerChar) return;

    if (UCharacterMovementComponent* Movement = OwnerChar->GetCharacterMovement())
        Movement->MaxWalkSpeed = ParseMoveSpeed(SpeedType);

    FVector Direction = OwnerChar->GetActorLocation() - TargetActor->GetActorLocation();
    Direction.Normalize();
    FVector TargetPos = TargetActor->GetActorLocation() + Direction * Distance;

    if (AAIController* AI = Cast<AAIController>(OwnerChar->GetController()))
    {
        AI->MoveToLocation(TargetPos, 50.f);
    }
}

void UNPCActionComponent::ExecuteWait(float Duration)
{
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
    {
        if (AController* C = OwnerChar->GetController())
        {
            C->StopMovement();
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Waiting for %.1f seconds"), *GetOwnerAgentID(), Duration);
}

void UNPCActionComponent::ExecuteDialogue(const FString& DialogueText, const EFacialState& EmotionID)
{
    // 표정 업데이트
    if (StateComponent)
    {
        StateComponent->SetFacialExpression(EmotionID);
    }

    UE_LOG(LogTemp, Log, TEXT("[Dialogue] %s (%d): \"%s\""), *GetOwnerAgentID(), (int32)EmotionID, *DialogueText);
    // Todo: Dialogue Widget, TTS 등
}

void UNPCActionComponent::ExecuteFaceRotate(FVector TargetLocation, float TurnSpeed)
{
    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
    {
        if (AAIController* AI = Cast<AAIController>(OwnerChar->GetController()))
        {
            AI->SetFocalPoint(TargetLocation);
        }
    }
}

void UNPCActionComponent::ExecutePerformAttack(const FString& AttackType)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s Attacks: %s"), *GetOwnerAgentID(), *AttackType);

    if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
    {
        if (AController* C = OwnerChar->GetController())
        {
            C->StopMovement();
        }
    }
    // Todo: Implement Attack (Damage, Animation)
}

void UNPCActionComponent::ExecuteDefend(bool bStartDefend)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s Defend: %s"), *GetOwnerAgentID(), bStartDefend ? TEXT("START") : TEXT("END"));
}

void UNPCActionComponent::ExecuteDodge()
{
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s Dodges"), *GetOwnerAgentID());
}

void UNPCActionComponent::ExecuteHandSignal(const FString& SignalName)
{
    if (!SignalName.IsEmpty())
    {
        PlayInteractionMontage(SignalName);
    }
    else
    {
        PlayInteractionMontage(TEXT("HandSignal_Wave"));
    }
}

void UNPCActionComponent::ExecuteEmote(const FString& EmoteName)
{
    if (!EmoteName.IsEmpty())
    {
        PlayInteractionMontage(EmoteName);
    }
}

// === Interaction System ===

void UNPCActionComponent::ExecuteInteraction(const FString& InteractionType, AActor* TargetActor, const FString& TargetID, const FString& ExtraParams)
{
    if (InteractionType.IsEmpty()) return;
    if (InteractionType.Equals(TEXT("Idle"), ESearchCase::IgnoreCase) ||
        InteractionType.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] Interaction: %s (Target: %s, ID: %s)"),
        *InteractionType, TargetActor ? *TargetActor->GetName() : TEXT("None"), *TargetID);

    // 1. Body State & Movement
    if      (InteractionType.Equals(NPCActionKeys::Interact_SitDown, ESearchCase::IgnoreCase))   ExecuteSitDown(TargetActor);
    else if (InteractionType.Equals(NPCActionKeys::Interact_SitUp, ESearchCase::IgnoreCase))     ExecuteSitUp();
    else if (InteractionType.Equals(NPCActionKeys::Interact_LieDown, ESearchCase::IgnoreCase))   ExecuteLieDown(TargetActor);
    else if (InteractionType.Equals(NPCActionKeys::Interact_LieUp, ESearchCase::IgnoreCase))     ExecuteLieUp();
    // 2. Inventory
    else if (InteractionType.Equals(NPCActionKeys::Interact_PickUp, ESearchCase::IgnoreCase))    ExecutePickUp(TargetActor);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Drop, ESearchCase::IgnoreCase))      ExecuteDropItem(TargetID);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Eat, ESearchCase::IgnoreCase))       ExecuteEat(TargetID);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Wear, ESearchCase::IgnoreCase))      ExecuteWear(TargetID);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Equip, ESearchCase::IgnoreCase))     ExecuteEquip(TargetID);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Unequip, ESearchCase::IgnoreCase))   ExecuteUnequip(TargetID);
    // 3. Task
    else if (InteractionType.Equals(NPCActionKeys::Interact_Clean, ESearchCase::IgnoreCase))     ExecuteClean(TargetActor);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Repair, ESearchCase::IgnoreCase))    ExecuteRepair(TargetActor);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Read, ESearchCase::IgnoreCase))      ExecuteRead(TargetActor);
    // 4. Performance
    else if (InteractionType.Equals(NPCActionKeys::Interact_Pray, ESearchCase::IgnoreCase))      ExecutePray();
    else if (InteractionType.Equals(NPCActionKeys::Interact_Dance, ESearchCase::IgnoreCase))     ExecuteDance(ExtraParams);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Sing, ESearchCase::IgnoreCase))      ExecuteSing(ExtraParams);
    else if (InteractionType.Equals(NPCActionKeys::Interact_HandSignal, ESearchCase::IgnoreCase))ExecuteHandSignal(ExtraParams);
    else if (InteractionType.Equals(NPCActionKeys::Interact_Emote, ESearchCase::IgnoreCase))     ExecuteEmote(ExtraParams);
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] Unknown Interaction: %s"), *InteractionType);
    }
}

// === Interaction Sub-Functions ===

void UNPCActionComponent::PlayInteractionMontage(const FString& Key)
{
    if (!InteractionData)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] InteractionData Asset NOT set!"));
        return;
    }

    UAnimMontage* Montage = InteractionData->FindMontage(Key);
    if (Montage)
    {
        if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
        {
            float Duration = OwnerChar->PlayAnimMontage(Montage);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] Playing Montage: %s (%.2fs)"), *Key, Duration);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] No Montage for Key: %s"), *Key);
    }
}

void UNPCActionComponent::ExecuteSitDown(AActor* TargetSeat)
{
    PlayInteractionMontage(NPCActionKeys::Interact_SitDown);
}

void UNPCActionComponent::ExecuteSitUp()
{
    PlayInteractionMontage(NPCActionKeys::Interact_SitUp);
}
void UNPCActionComponent::ExecuteLieDown(AActor* TargetBed)
{
    PlayInteractionMontage(NPCActionKeys::Interact_LieDown);
}

void UNPCActionComponent::ExecuteLieUp()
{
    PlayInteractionMontage(NPCActionKeys::Interact_LieUp);
}

void UNPCActionComponent::ExecutePickUp(AActor* TargetItem)
{
    PlayInteractionMontage(NPCActionKeys::Interact_PickUp);

    if (TargetItem)
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] Picked up %s (Visual only)"), *TargetItem->GetName());
        TargetItem->Destroy();
    }
}

void UNPCActionComponent::ExecuteDropItem(const FString& ItemID)
{
    if (InventoryComponent)
    {
        if (InventoryComponent->RemoveItem(ItemID, 1))
        {
            PlayInteractionMontage(NPCActionKeys::Interact_Drop);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] Dropped: %s"), *ItemID);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[NPCAction] Drop failed (Not found): %s"), *ItemID);
        }
    }
}

void UNPCActionComponent::ExecuteEat(const FString& ItemID)
{
    if (InventoryComponent)
    {
        if (InventoryComponent->RemoveItem(ItemID, 1))
        {
            PlayInteractionMontage(NPCActionKeys::Interact_Eat);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] Ate: %s"), *ItemID);
            // Todo: Health/Hunger Restore
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[NPCAction] No item to eat: %s"), *ItemID);
        }
    }
}

void UNPCActionComponent::ExecuteWear(const FString& ItemID)
{
    if (InventoryComponent && InventoryComponent->HasItem(ItemID))
    {
        PlayInteractionMontage(NPCActionKeys::Interact_Wear);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] Wearing: %s"), *ItemID);
    }
}

void UNPCActionComponent::ExecuteEquip(const FString& ItemID)
{
    if (InventoryComponent)
    {
        InventoryComponent->EquipItem(ItemID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] Equipped: %s"), *ItemID);
    }
}

void UNPCActionComponent::ExecuteUnequip(const FString& ItemID)
{
    if (InventoryComponent)
    {
        InventoryComponent->UnequipItemByID(ItemID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] Unequipped: %s"), *ItemID);
    }
}

void UNPCActionComponent::ExecuteClean(AActor* TargetZone)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Clean);
}

void UNPCActionComponent::ExecuteRepair(AActor* TargetObject)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Repair);
}

void UNPCActionComponent::ExecuteRead(AActor* TargetBook)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Read);
}

void UNPCActionComponent::ExecutePray()
{
    PlayInteractionMontage(NPCActionKeys::Interact_Pray);
}

void UNPCActionComponent::ExecuteDance(const FString& Style)
{
    FString Key = NPCActionKeys::Interact_Dance;
    if (!Style.IsEmpty())
    {
        Key = Key + TEXT("_") + Style;
    }
    PlayInteractionMontage(Key);
}

void UNPCActionComponent::ExecuteSing(const FString& SongName)
{
    PlayInteractionMontage(NPCActionKeys::Interact_Sing);
}
