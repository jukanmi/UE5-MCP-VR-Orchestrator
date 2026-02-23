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
    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
        {
            BB->SetValueAsEnum(ASmartNPCAIController::Key_BehaviorMode, (uint8)Batch.Mode);
        }
    }

    // 1. Actions 분배 (Dialogue = 즉시, 나머지 = Queue)
    // 개별 액션의 FacialState는 Dispatch/Queue 처리 시 업데이트됩니다.
    DispatchActions(Batch.Actions);

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - Batch Executed. Actions: %d"),
        *GetOwnerAgentID(), Batch.Actions.Num());
}

/**
 * @brief 감정 상태를 업데이트합니다.
 * @param Action 업데이트할 액션
 */
void UNPCActionComponent::UpdateActionState(const FGameAction& Action)
{
    // FacialState 업데이트 (이제 Enum 타입)
    if (StateComponent)
    {
        StateComponent->SetFacialExpression(Action.FacialState);
    }
}
/**
 * @brief 액션을 분배합니다.
 * @param Actions 분배할 액션
 */
void UNPCActionComponent::DispatchActions(const TArray<FGameAction>& Actions)
{
    for (const FGameAction& Action : Actions)
    {
        // 1. Critical Stop
        if (Action.ActionType == EAction::Stop)
        {
            StopAllActions();
            continue;
        }

        // 2. Parallel Action: Dialogue (즉시 실행, 큐잉하지 않음)
        if (Action.ActionType == EAction::Dialogue && !bIsDialogueActive)
        {
            bIsDialogueActive = true;
            // 대화는 즉시 감정/상태 변화를 동반할 수 있음
            UpdateActionState(Action);

            FString TextContent = Action.Parameters.FindRef(NPCActionKeys::Key_Text);
            EFacialState Emotion = Action.FacialState;

            BaseDialogue(TextContent, Emotion);
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
        
        FString ActionStr = UEnum::GetValueAsString(Action.ActionType);
        FString SubActionName;
        ActionStr.Split(TEXT("::"), nullptr, &SubActionName);
        if (SubActionName.IsEmpty()) SubActionName = ActionStr;
        
        if (StateComponent) StateComponent->CurrentActionID = SubActionName;
        
        // 물리적 액션 시작 전 상태(Facial) 업데이트
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
        BB->SetValueAsString(ASmartNPCAIController::Key_SubAction, SubActionName);

        // Parameters → JSON 문자열로 변환하여 Blackboard에 저장
        TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
        for (const auto& Pair : Action.Parameters)
        {
            JsonObj->SetStringField(Pair.Key, Pair.Value);
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

        UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Starting Action '%s'"), *GetOwnerAgentID(), *SubActionName);
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

// ============================================================================
// [기본 함수 (Base Functions)] 래퍼함수 구현시 사용하는 유틸 함수
// ============================================================================

void UNPCActionComponent::BaseMove(FVector TargetLocation, EMoveType SpeedType, float AcceptanceRadius)
{
    // [의도] 이동 속도(Walk, Run 등)에 맞춰 물리 컴포넌트의 설정값을 변경시킨 후, 지정된 목적지로 AI 이동을 호출하여 자연스러운 이동을 유도합니다.
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;

    if (UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement())
    {
        MovementComp->MaxWalkSpeed = ParseMoveSpeed(SpeedType);
    }

    if (AAIController* AIController = Cast<AAIController>(OwnerCharacter->GetController()))
    {
        AIController->MoveToLocation(TargetLocation, AcceptanceRadius);
    }
}

void UNPCActionComponent::BaseEmotion(const EFacialState Emotion)
{
    // [의도] 얼굴 표정(Emotion) 상태를 동기화하여, 감정적 뉘앙스를 플레이어에게 직관적으로 전달하기 위함입니다.
    if (StateComponent)
    {
        StateComponent->SetFacialExpression(Emotion);
    }
}

void UNPCActionComponent::BaseDialogue(const FString& DialogueText, const EFacialState Emotion)
{
    // [의도] 텍스트 대화 출력과 함께 얼굴 표정(Emotion) 상태를 동기화하여, 대화 내용에 맞는 감정적 뉘앙스를 플레이어에게 직관적으로 전달하기 위함입니다.
    BaseEmotion(Emotion);

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 대화 실행: %s (표정: %d) - 내용: %s"), 
        *GetOwnerAgentID(), (int32)Emotion, *DialogueText);
}

void UNPCActionComponent::BaseFaceRotate(FVector TargetLocation, float TurnSpeed)
{
    // [의도] 대상을 향해 몸과 시선을 돌려 상호작용 의지나 목적을 시각적으로 강하게 표현하기 위함입니다.
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;

    if (AAIController* AIController = Cast<AAIController>(OwnerCharacter->GetController()))
    {
        AIController->SetFocalPoint(TargetLocation);
    }
}

void UNPCActionComponent::BaseSitDown(AActor* TargetSeat)
{
    // [의도] 맵 상의 특정 객체(의자, 벤치 등)에 맞춰 앉는 애니메이션 처리를 수행하기 위함입니다.
    if (StateComponent) StateComponent->bIsSit = true;
    BasePlayMontage(NPCActionKeys::Interact_SitDown);
}

void UNPCActionComponent::BaseSitUp()
{
    // [의도] 앉아 있는 상태를 해제하고 다시 일반적인 활동이 가능한 유휴 상태로 복귀시키기 위함입니다.
    if (StateComponent) StateComponent->bIsSit = false;
    BasePlayMontage(NPCActionKeys::Interact_SitUp);
}

void UNPCActionComponent::BaseLieDown(AActor* TargetBed)
{
    // [의도] 침대 등의 공간에서 눕는 동작을 연출하여, 낮잠이나 수면 등의 휴식 상태를 직관적으로 표현하기 위함입니다.
    if (StateComponent) StateComponent->bIsLie = true;
    BasePlayMontage(NPCActionKeys::Interact_LieDown);
}

void UNPCActionComponent::BaseLieUp()
{
    // [의도] 누워있는 취침 상태에서 기상하여 활동을 재개하기 전의 준비 동작을 처리하기 위함입니다.
    if (StateComponent) StateComponent->bIsLie = false;
    BasePlayMontage(NPCActionKeys::Interact_LieUp);
}

void UNPCActionComponent::BaseStopCurrentAction()
{
    // [의도] 예상치 못한 위협이나 상위 우선도의 행동이 들어왔을 때, 현재 수행 중이던 모든 물리적/시각적 행동을 멈추고 리셋하기 위함입니다.
    AbortCurrentAction();
}

TMap<FString, int32> UNPCActionComponent::BaseDetectEntityInRange(float Range, EEntityType EntityType)
{
    // [의도] 지정된 반경(Range) 내에서 특정 속성(적, 아이템 등)의 액터를 탐지하여, 후속 상호작용을 위한 대상을 선별하기 위함입니다.
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return TMap<FString, int32>();

    TMap<FString, int32> DetectedEntities;
    // TODO: 실제 Overlap 혹은 Collision 체킹을 기반으로 탐지 및 결과 반환 로직 구현
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: 반경 %.1f 내의 엔티티(타입 %d) 탐색 시도"), *GetOwnerAgentID(), Range, (int32)EntityType);
    return DetectedEntities;
}

void UNPCActionComponent::BaseSendEventToActor(AActor* TargetActor, const FString& EventName)
{
    // [의도] 플레이어나 타 액터에게 특정 메시지를 던져, 협동이나 대립 같은 복합적인 에코시스템을 유기적으로 연동시키기 위함입니다.
    if (!TargetActor) return;

    // TODO: 인터페이스 통신이나 이벤트 브로드캐스트 구현
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s가 %s에게 이벤트 '%s' 전달"), *GetOwnerAgentID(), *TargetActor->GetName(), *EventName);
}

void UNPCActionComponent::BasePlayMontage(const FString& MontageName)
{
    // [의도] NPCActionKeys 등에서 넘겨받은 몽타주 키를 기반으로 설정된 Asset을 재생하여 상태의 변화를 가시화하기 위함입니다.
    if (!InteractionData)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] InteractionData Asset 설정 누락!"));
        return;
    }

    if (UAnimMontage* Montage = InteractionData->FindMontage(MontageName))
    {
        if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
        {
            float Duration = OwnerCharacter->PlayAnimMontage(Montage);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] 몽타주 재생: %s (에상시간: %.2f초)"), *MontageName, Duration);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 지정한 몽타주를 찾을 수 없음: %s"), *MontageName);
    }
}

void UNPCActionComponent::BasePlaySound(const FString& SoundName)
{
    // [의도] 시각적 몽타주뿐만 아니라 효과음이나 보이스 등 청각적인 피드백을 통해 몰입감을 더해주기 위함입니다.
    // TODO: SoundBase 검색 후 UGameplayStatics::PlaySoundAtLocation 등 실행 로직 작성
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 사운드 재생: %s"), *SoundName);
}

// ----------------------------------------------------------------------------
// [EAction 래퍼 함수 (Action Wrappers)]
// ----------------------------------------------------------------------------

// ==========================================
// [1] Common Behaviors
// ==========================================

// [의도] 아무 동작도 하지 않는 대기 상태입니다. 현재 수행 중인 물리적/시각적 동작을 안전하게 초기화합니다.
void UNPCActionComponent::ExecuteIdle() 
{
    BaseStopCurrentAction();
}

// [의도] 목표 위치 혹은 대상 액터를 향해 이동합니다. 동적인 타겟 액터가 존재할 경우 우선적으로 추적합니다.
void UNPCActionComponent::ExecuteMove(FVector TargetLocation, AActor* TargetActor, EMoveType SpeedType) 
{
    if (TargetActor)
    {
        BaseMove(TargetActor->GetActorLocation(), SpeedType);
    }
    else
    {
        BaseMove(TargetLocation, SpeedType);
    }
}

// [의도] 지정된 타겟 액터를 일정 간격을 두고 따라다닙니다. 호위나 감시 등의 상황에 유용합니다.
void UNPCActionComponent::ExecuteFollow(AActor* TargetActor, EMoveType SpeedType) 
{
    if (TargetActor)
    {
        ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
        if (OwnerCharacter)
        {
            FVector Direction = OwnerCharacter->GetActorLocation() - TargetActor->GetActorLocation();
            Direction.Normalize();
            FVector FollowPos = TargetActor->GetActorLocation() + Direction * 300.f; // 300 유닛 거리 유지

            BaseMove(FollowPos, SpeedType);
        }
    }
}

// [의도] 대상과 텍스트 형태의 대화를 시작하며, 대화 내용의 뉘앙스에 맞는 표정을 출력합니다.
void UNPCActionComponent::ExecuteDialogue(const FString& DialogueText, const EFacialState Emotion)
{
    BaseDialogue(DialogueText, Emotion);
}

// [의도] 특정 위치나 타겟(우선)을 바라보도록 몸을 부드럽게 회전시켜, 상호작용 의지나 집중을 시각적으로 나타냅니다.
void UNPCActionComponent::ExecuteTurnTo(FVector TargetLocation, AActor* TargetActor) 
{
    FVector FocusLocation = TargetActor ? TargetActor->GetActorLocation() : TargetLocation;
    BaseFaceRotate(FocusLocation);
}

// [의도] 주변을 무작위로 둘러보며 경계하거나 탐색하는 행동입니다. 타겟이 있다면 타겟 주변을 확인합니다.
void UNPCActionComponent::ExecuteScan(FVector TargetLocation, AActor* TargetActor) 
{
    FVector FocusLocation = TargetActor ? TargetActor->GetActorLocation() : TargetLocation;
    FVector RandomOffset = FVector(FMath::VRand().X, FMath::VRand().Y, 0.0f) * 100.0f;
    BaseFaceRotate(FocusLocation + RandomOffset, 3.0f);
}

// [의도] 인벤토리에서 지정된 소모성 아이템(예: 음식, 물약)을 꺼내 소비합니다.
void UNPCActionComponent::ExecuteUseItem(const FString& ItemID) 
{
    if (InventoryComponent && InventoryComponent->RemoveItem(ItemID, 1))
    {
        BasePlayMontage(TEXT("Eat")); // 차후 ItemID에 따른 몽타주 연동 기능으로 고도화 가능
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 사용(소모): %s"), *ItemID);
    }
}

// [의도] 인벤토리에 보유 중인 지정된 장비 아이템을 몸에 착용하거나 손에 장비합니다.
void UNPCActionComponent::ExecuteEquipAction(const FString& ItemID) 
{
    if (InventoryComponent)
    {
        InventoryComponent->EquipItem(ItemID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 장착: %s"), *ItemID);
    }
}

// [의도] 현재 착용/장착 중인 아이템을 해제하여 다시 가방(인벤토리)에 넣습니다.
void UNPCActionComponent::ExecuteUnequipAction(const FString& ItemID) 
{
    if (InventoryComponent)
    {
        InventoryComponent->UnequipItemByID(ItemID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 해제: %s"), *ItemID);
    }
}

// ==========================================
// [2] Combat Behaviors
// ==========================================

// [의도] 대상을 향해 전투 행위를 수행합니다. 타겟을 바라보며 접근한 뒤, 공격 몽타주와 판정을 발생시킵니다.
void UNPCActionComponent::ExecuteAttackAction(AActor* TargetActor, EAttackType AttackType) 
{
    if (TargetActor)
    {
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
        BaseMove(TargetActor->GetActorLocation(), EMoveType::Run);
        BasePlayMontage(TEXT("Attack")); // 기본 공격 몽타주 재생 (추후 무기 타입별 확장 가능)
    }
}

// [의도] 타겟의 물리적 공격에 대비하여 방어 자세를 취하고 대미지 감소를 도모합니다.
void UNPCActionComponent::ExecuteBlock(AActor* TargetActor) 
{
    if (TargetActor)
    {
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    }
    BasePlayMontage(TEXT("Block")); // 방어 몽타주 재생
}

// [의도] 들어오는 피격판정을 회피하기 위해 지정된 방향(안전지대)으로 기민하게 움직입니다.
void UNPCActionComponent::ExecuteDodgeAction(FVector Direction) 
{
    // [설명] 단순 제자리 회피 애니메이션을 재생하는 방식.
    // 더 정교한 회피는 Direction 방향에 맞춰 RootMotion이나 Impulse를 줄 수도 있습니다.
    BaseTurnTo(FVector::ZeroVector, Direction);
    BasePlayMontage(TEXT("Dodge")); 
}

// [의도] 심각한 위협으로부터 벗어나기 위해 다급히 달아납니다. (Run 스피드 강제 적용)
void UNPCActionComponent::ExecuteFlee(FVector EscapeLocation) 
{
    BaseMove(EscapeLocation, EMoveType::Run);
}

// [의도] 아군에게 시각적 수신호를 보내어 전투 상황, 대기, 돌격 등을 지시합니다.
void UNPCActionComponent::ExecuteSignalAllies(const FString& HandSign) 
{
    if (!HandSign.IsEmpty())
    {
        BasePlayMontage(HandSign);
    }
    else
    {
        BasePlayMontage(TEXT("HandSignal_Wave"));
    }
}

// ==========================================
// [3] Social Behaviors
// ==========================================

// [의도] 다른 캐릭터와 물물교환을 시도합니다. 내 아이템 소모 및 상대와의 상호작용을 처리합니다.
void UNPCActionComponent::ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, int32 GiveAmount, const FString& GetItemID, int32 GetAmount) 
{
    // TODO: 인벤토리 아이템 GetItemCount로 개수 확인 후 GiveItem 실행
    ExecuteGiveItem(TargetActor, GiveItemID, GiveAmount);
}

// [의도] 대가 없이 다른 캐릭터에게 내 소유의 아이템을 건네줍니다.
void UNPCActionComponent::ExecuteGiveItem(AActor* TargetActor, const FString& ItemID, int32 Amount) 
{
    if (InventoryComponent && InventoryComponent->HasItem(ItemID, Amount))
    {
        InventoryComponent->RemoveItem(ItemID, Amount);
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
        BasePlayMontage(TEXT("Give"));
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 전달 완료: %s"), *ItemID);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCAction] 아이템 없음: %s"), *ItemID);
    }
}

// [의도] 불안해하거나 상처받은 대상을 다독여 정신적 상태를 회복시킵니다.
void UNPCActionComponent::ExecuteComfort(AActor* TargetActor) 
{
    ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    BasePlayMontage(TEXT("Comfort"));
}

// [의도] 가방에서 물건을 잠시 손에 들고 대상에게 무언가를 보여주거나 설명합니다.
void UNPCActionComponent::ExecuteHandObject(const FString& ItemID) 
{
    // 인벤토리에 아이템이 있는지 일차로 확인
    if (InventoryComponent && InventoryComponent->HasItem(ItemID))
    {
        InventoryComponent->EquipItem(ItemID); // 임시로 장착 형태로 시각화
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 제시: %s"), *ItemID);
    }
}

// ==========================================
// [4] Task Behaviors
// ==========================================

// [의도] 지정된 위치 근방에 떨어진 물건 액터를 탐색 후 인벤토리에 수집합니다.
void UNPCActionComponent::ExecutePickUp(FVector Location) 
{
    BaseMove(Location, EMoveType::Walk);
    BasePlayMontage(TEXT("PickUp"));
    // TODO: 주변 아이템 Overlap 체크 후 InventoryComponent->AddItem 처리 추가
    TMap<FString, int32> DetectedEntities = BaseDetectEntityInRange(100.0f, EEntityType::Item);
    for (const TPair<FString, int32>& Entity : DetectedEntities)
    {
        InventoryComponent->AddItem(Entity.Key, Entity.Value);
    }
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 줍기 성공: %s"), *ItemID);
}

// [의도] 인벤토리에서 지정된 아이템을 즉각 물리적 공간에 내려놓습니다.
void UNPCActionComponent::ExecuteDrop(const FString& ItemID) 
{
    if (!InventoryComponent) return;

    if (InventoryComponent->RemoveItem(ItemID, 1))
    {
        BasePlayMontage(TEXT("Drop"));
        // TODO: 세상에 실제 액터를 스폰(Drop)하는 로직 추가
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 버리기 성공: %s"), *ItemID);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 아이템 버리기 실패 (미보유): %s"), *ItemID);
    }
}

// [의도] 인벤토리의 재료들을 소모하여 새로운 결과물을 합성해냅니다.
void UNPCActionComponent::ExecuteCraft(const TArray<FString>& ItemIDs) 
{
    // TODO: 제작 레시피 검증 및 아이템 소모/생성 로직 연동
    BasePlayMontage(TEXT("Craft"));
}

// [의도] 파손된 장비 혹은 객체의 내구도를 복구합니다.
void UNPCActionComponent::ExecuteRepair(const FString& ItemID) 
{
    // TODO: 부서진 아이템 혹은 오브젝트 상태 복구
    BasePlayMontage(TEXT("Repair"));
    // TODO: 내구도 회복 로직 추가
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 수리 완료: %s"), *ItemID);
    if (InventoryComponent && InventoryComponent->HasItem(ItemID))
    {
        InventoryComponent->RepairItem(ItemID, Statement.CurrentStats.Perception);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 아이템 수리 실패 (미보유): %s"), *ItemID);
    }
}

// ==========================================
// [5] Investigation Behaviors
// ==========================================

// [의도] 소음이나 단서가 발생한 지점으로 조심스럽게 이동 후, 면밀한 수색 모션을 진행합니다.
void UNPCActionComponent::ExecuteInvestigate(FVector Location) 
{
    BaseMove(Location, EMoveType::Walk);
    ExecuteScan(Location, nullptr); // 주변 둘러보기 연계
}

// [의도] 두 지정된 지점 사이를 정찰하며 위협 요소를 파악합니다.
void UNPCActionComponent::ExecuteScout(FVector StartLocation, FVector EndLocation) 
{
    // [설명] 단순하게 목적지로 보내는 형태입니다. 
    // 나중엔 BehaviorTree의 Loop 구조와 결합해 왕복하게끔 설계해야 함
    // 현위치에서 StartLocation, EndLocation중 가까운걸 찾기

    BaseMove(EndLocation, EMoveType::Walk);
}

// ==========================================
// [6] Lifestyle Behaviors
// ==========================================

// [의도] 의자나 쉼터(Entity)를 대상으로 이동한 후 착석 애니메이션을 수행합니다.
void UNPCActionComponent::ExecuteSit(AActor* TargetEntity) 
{
    if (TargetEntity)
    {
        BaseMove(TargetEntity->GetActorLocation(), EMoveType::Walk);
        ExecuteTurnTo(FVector::ZeroVector, TargetEntity);
    }
    BaseSitDown(TargetEntity);
}

// [의도] 침대나 바닥을 대상으로 이동한 후 눕는 모션을 수행해 휴식(Sleep) 상태로 진입합니다.
void UNPCActionComponent::ExecuteSleep(AActor* TargetEntity) 
{
    if (TargetEntity)
    {
        BaseMove(TargetEntity->GetActorLocation(), EMoveType::Walk);
        ExecuteTurnTo(FVector::ZeroVector, TargetEntity);
    }
    BaseLieDown(TargetEntity);
}

// [의도] 지정된 반경 내 환경을 청소하거나 빗자루질 같은 정리 애니메이션을 반복합니다.
void UNPCActionComponent::ExecuteClean(FVector Location, float Radius) 
{
    BaseMove(Location, EMoveType::Walk);
    BasePlayMontage(TEXT("Clean"));
}

// [의도] 문서를 들여다보거나 책을 읽는 모션을 통해 정보 습득 행동을 가시화합니다.
void UNPCActionComponent::ExecuteRead(AActor* TargetEntity)
{
    BasePlayMontage(TEXT("Read"));
}

// [의도] 특정 장소를 향해 경건한 자세를 취하며 기도를 올립니다.
void UNPCActionComponent::ExecutePray(FVector Location, AActor* TargetEntity)
{
    BaseMove(Location, EMoveType::Walk, 100.0f);
    ExecuteTurnTo(Location, TargetEntity);
    BasePlayMontage(TEXT("Pray"));
}

// [의도] 분위기 환기 및 사교적 교류를 위해 춤을 춥니다.
void UNPCActionComponent::ExecuteDance(const FString& DanceName)
{
    if (!DanceName.IsEmpty())
    {
        BasePlayMontage(DanceName);
    }
    else
    {
        BasePlayMontage(TEXT("Dance_Basic"));
    }
}

// [의도] 노래를 부르며 다른 캐릭터들의 이목을 끌거나 오락을 제공합니다. (오디오 연동 필요)
void UNPCActionComponent::ExecuteSing(const FString& SingName)
{
    BasePlayMontage(TEXT("Sing_Basic"));
    if (!SingName.IsEmpty())
    {
        BasePlaySound(SingName);
    }
}


