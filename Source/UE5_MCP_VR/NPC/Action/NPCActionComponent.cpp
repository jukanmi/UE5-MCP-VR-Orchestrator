#include "NPCActionComponent.h"
#include "../NPCStateComponent.h"
#include "../NPCInventoryComponent.h"
#include "SmartNPCAIController.h"
#include "../Struct/NPCActionKeys.h"
#include "../NPCActionDataAsset.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameInstance.h"
#include "../../Inventory/ItemManager.h"
#include "Perception/AISense_Hearing.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "../SmartNPC.h"

namespace
{
    FName GetGameplayTagForAction(EAction ActionType)
    {
        switch(ActionType)
        {
            case EAction::Move:
            case EAction::Follow:
            case EAction::Scan:
            case EAction::Investigate:
                return FName("State.Action.Move.Run");
            case EAction::TurnTo:
                return FName("State.Action.TurnTo");
            case EAction::UseItem:
            case EAction::Equip:
            case EAction::Unequip:
            case EAction::PickUp:
            case EAction::Drop:
            case EAction::Repair:
                return FName("State.Action.Interact");
            
            case EAction::Attack:
            case EAction::Magic:
            case EAction::RangeAttack:
                return FName("State.Action.Combat.Attack");
            case EAction::Block:
            case EAction::Dodge:
                return FName("State.Action.Combat.Dodge");
            case EAction::Flee:
                return FName("State.Action.Combat.Flee");
            case EAction::SignalAllies:
                return FName("State.Action.Social.Signal");

            case EAction::Dialogue:
                return FName("State.Action.Social.Dialogue");
            case EAction::Comfort:
            case EAction::Emote:
            case EAction::HandObject:
            case EAction::Trade:
                return FName("State.Action.Social.Emote");
            
            case EAction::Dance:
            case EAction::Sing:
                return FName("State.Action.Social.Dance");
            
            default:
                return NAME_None;
        }
    }
}

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

    if (Type == EMoveType::Walk)   return StateComponent->GetCurrentStats().Movement.WalkSpeed;
    if (Type == EMoveType::Run)    return StateComponent->GetCurrentStats().Movement.RunSpeed;
    if (Type == EMoveType::Sprint) return StateComponent->GetCurrentStats().Movement.SprintSpeed;
    if (Type == EMoveType::Crouch) return StateComponent->GetCurrentStats().Movement.CrouchSpeed;
    return StateComponent->GetCurrentStats().Movement.WalkSpeed;
}

// === Action Batch System ===


void UNPCActionComponent::ExecuteActionBatch(const FActionBatch& Batch)
{
    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
        {
            BB->SetValueAsEnum(ASmartNPCAIController::Key_BehaviorMode, (uint8)Batch.Mode);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - Batch Executed. Actions: %d"),
            *GetOwnerAgentID(), Batch.Actions.Num());
        }
    }

    // [의도(Why)] 대화 액션은 이동 등의 물리적인 행동과 병동 실행(단기 병렬 큐)되어야 하므로 따로 처리하고, 나머지는 물리 액션 큐에 순차 적재합니다.
    DispatchActions(Batch.Actions);
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
        // [의도(Why)] 모든 동작 중지(Emergency Stop) 등 최우선 순위는 즉각 반영하여 불필요한 연산을 막습니다.
        if (Action.ActionType == EAction::Stop)
        {
            ExecuteIdle();
            continue;
        }

        // [의도(Why)] 대화는 걸으면서도 할 수 있어야 하므로 큐 시스템 밖에서 비동기적(즉시)으로 실행시킵니다.
        if (Action.ActionType == EAction::Dialogue && !bIsDialogueActive)
        {
            bIsDialogueActive = true;
            FString TextContent = Action.Parameters.FindRef(NPCActionKeys::Key_Text);
            BaseDialogue(TextContent, Action.FacialState);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - Dialogue Executed. Text: %s"), *GetOwnerAgentID(), *TextContent);
        }
        else
        {
            // [의도(Why)] 일반 물리적 액션은 이전 행동이 끝나길 기다렸다가 순차적으로 실행(Queue)되도록 보장합니다.
            ActionQueue.Enqueue(Action);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - Action Queued. Action: %s"),
            *GetOwnerAgentID(), *UEnum::GetValueAsString(Action.ActionType));
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

    if (StateComponent) StateComponent->SetCurrentActionType(EAction::Idle);

    ResetAllStateTagsToIdle(GetOwner());

    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        AI->StopMovement();
    }
    
    // [FIX] 결합도를 낮추기 위해 직접 BlackBoard를 수정하지 않고 델리게이트 브로드캐스트
    OnActionStoppedAll.Broadcast();

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Stopped All Actions."), *GetOwnerAgentID());
}

void UNPCActionComponent::ProcessNextAction()
{
    if (bIsBusy || ActionQueue.IsEmpty()) return;

    if (ActionQueue.Dequeue(CurrentAction))
    {
        bIsBusy = true;
        
        if (StateComponent) StateComponent->SetCurrentActionType(CurrentAction.ActionType);
        
        TransitionStateTag(GetOwner(), CurrentAction.ActionType);

        // 물리적 액션 시작 전 상태(Facial) 업데이트
        UpdateActionState(CurrentAction);

        // [FIX] 결합도를 낮추기 위해 직접 BlackBoard를 수정하지 않고 델리게이트 브로드캐스트
        OnActionStarted.Broadcast(CurrentAction);
        
        // target_loc 특수 처리는 여전히 남겨두지만 의존성이 낮아지면 이동 가능
        if (ASmartNPCAIController* AI = GetOwnerAIController())
        {
            if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
            {
                if (CurrentAction.Parameters.Contains(NPCActionKeys::Key_TargetLoc))
                {
                    TryParseAndSetTargetLocation(BB, CurrentAction.Parameters[NPCActionKeys::Key_TargetLoc]);
                }
            }
        }

        UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Starting Action '%s'"), *GetOwnerAgentID(), *UEnum::GetValueAsString(CurrentAction.ActionType));
    }
}

void UNPCActionComponent::OnActionCompleted()
{
    bIsBusy = false;
    
    EAction CompletedAction = StateComponent ? StateComponent->GetCurrentActionType() : EAction::Idle;
    FString CompletedActionStr = UEnum::GetValueAsString(CompletedAction);

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Action '%s' Completed."), *GetOwnerAgentID(), *CompletedActionStr);

    RevertStateTagToIdle(GetOwner(), CompletedAction);

    if (StateComponent) StateComponent->SetCurrentActionType(EAction::Idle);

    // 다음 큐 항목 처리
    ProcessNextAction();

    // 더 이상 큐에 처리할 액션이 없다면 HasAction 플래그를 해제 (Decorator 제어용)
    if (!bIsBusy)
    {
        if (ASmartNPCAIController* AI = GetOwnerAIController())
        {
            if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
            {
                BB->SetValueAsBool(ASmartNPCAIController::Key_HasAction, false);
            }
        }
    }
}

void UNPCActionComponent::AbortCurrentAction()
{
    // TODO: [UE5] 액션 실행 실패(예: 길찾기 실패, 타겟 없음 등) 시 Python으로 action_failed 이벤트를 전송해야 합니다.
    // FEnvelopeBuilder::BuildActionFailed(RefMsgId, PayloadJson)를 사용하여 에러 원인과 실패한 명령의 메타데이터를 백엔드에 알려주세요.
    // 이를 위해서는 ExecuteAction/ActionQueue 처리 시 Python이 전달한 msg_id를 보관해야 할 수 있습니다.

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: ABORTING Action"), *GetOwnerAgentID());
    
    EAction AbortedAction = StateComponent ? StateComponent->GetCurrentActionType() : EAction::Idle;

    RevertStateTagToIdle(GetOwner(), AbortedAction);

    if (StateComponent) StateComponent->SetCurrentActionType(EAction::Idle);

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
    // 이동 속도(Walk, Run 등)에 맞춰 물리 컴포넌트의 설정값을 변경시킨 후, 지정된 목적지로 AI 이동을 호출하여 자연스러운 이동을 유도합니다.
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
    // 얼굴 표정(Emotion) 상태를 동기화하여, 감정적 뉘앙스를 플레이어에게 직관적으로 전달하기 위함입니다.
    if (StateComponent)
    {
        StateComponent->SetFacialExpression(Emotion);
    }
}

void UNPCActionComponent::BaseDialogue(const FString& DialogueText, const EFacialState Emotion)
{
    // 텍스트 대화 출력과 함께 얼굴 표정(Emotion) 상태를 동기화하여, 대화 내용에 맞는 감정적 뉘앙스를 플레이어에게 직관적으로 전달하기 위함입니다.
    BaseEmotion(Emotion);

    // 대화 발생 시 주변(NPC 등)이 듣고 반응할 수 있도록 소음 이벤트 등록
    if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
    {
        UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_Dialogue, OwnerCharacter, 0.0f, NPCActionKeys::NoiseTag_Dialogue);
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 대화 실행: %s (표정: %d) - 내용: %s"), 
        *GetOwnerAgentID(), (int32)Emotion, *DialogueText);
}

void UNPCActionComponent::BaseFaceRotate(FVector TargetLocation, float TurnSpeed)
{
    // 대상을 향해 몸과 시선을 돌려 상호작용 의지나 목적을 시각적으로 강하게 표현하기 위함입니다.
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;

    if (AAIController* AIController = Cast<AAIController>(OwnerCharacter->GetController()))
    {
        AIController->SetFocalPoint(TargetLocation);
    }
}

void UNPCActionComponent::BaseSitDown(AActor* TargetSeat)
{
    // 맵 상의 특정 객체(의자, 벤치 등)에 맞춰 앉는 애니메이션 처리를 수행하기 위함입니다.
    if (StateComponent) StateComponent->bIsSit = true;
    BasePlayActionMedia(NPCActionKeys::Interact_SitDown);
}

void UNPCActionComponent::BaseSitUp()
{
    // 앉아 있는 상태를 해제하고 다시 일반적인 활동이 가능한 유휴 상태로 복귀시키기 위함입니다.
    if (StateComponent) StateComponent->bIsSit = false;
    BasePlayActionMedia(NPCActionKeys::Interact_SitUp);
}

void UNPCActionComponent::BaseLieDown(AActor* TargetBed)
{
    // 침대 등의 공간에서 눕는 동작을 연출하여, 낮잠이나 수면 등의 휴식 상태를 직관적으로 표현하기 위함입니다.
    if (StateComponent) StateComponent->bIsLie = true;
    BasePlayActionMedia(NPCActionKeys::Interact_LieDown);
}

void UNPCActionComponent::BaseLieUp()
{
    // 누워있는 취침 상태에서 기상하여 활동을 재개하기 전의 준비 동작을 처리하기 위함입니다.
    if (StateComponent) StateComponent->bIsLie = false;
    BasePlayActionMedia(NPCActionKeys::Interact_LieUp);
}

void UNPCActionComponent::BaseStopCurrentAction()
{
    // 예상치 못한 위협이나 상위 우선도의 행동이 들어왔을 때, 현재 수행 중이던 모든 물리적/시각적 행동을 멈추고 리셋하기 위함입니다.
    AbortCurrentAction();
}

void UNPCActionComponent::BaseSignalAllies(const FString& SignAssetID)
{
    // 전투 등에서 하체 이동 방해 없이 상체 몽타주(수신호) 재생
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s가 수신호를 보냅니다: %s"), *GetOwnerAgentID(), *SignAssetID);
    BasePlayActionMedia(SignAssetID); 
}

void UNPCActionComponent::BaseComfort(AActor* TargetActor)
{
    // 대상을 위로하는 로직. 향후 IK 보정이 필요한 경우 위치 계산 추가.
    if (TargetActor)
    {
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s가 %s를 위로합니다."), *GetOwnerAgentID(), *TargetActor->GetName());
    }
    BasePlayActionMedia(TEXT("Comfort"));
}

void UNPCActionComponent::BaseEmote(const FString& EmoteAssetID)
{
    // 손인사, 끄덕임 등의 감정표현
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s 몸짓: %s"), *GetOwnerAgentID(), *EmoteAssetID);
    BasePlayActionMedia(EmoteAssetID);
}

void UNPCActionComponent::BaseDance(const FString& DanceAssetID)
{
    // 무한 루프로 춤추는 동작
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s 춤추기: %s"), *GetOwnerAgentID(), *DanceAssetID);
    BasePlayActionMedia(DanceAssetID);
}

void UNPCActionComponent::BaseSing(const FString& SingAssetID)
{
    // 노래하거나 허밍 (Montage와 Sound 동시 재생)
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s 노래하기: %s"), *GetOwnerAgentID(), *SingAssetID);
    BasePlayActionMedia(SingAssetID);
}

TMap<FString, int32> UNPCActionComponent::BaseDetectEntityInRange(float SearchRadius, EEntityType TargetEntityType)
{
    // 특정 반경 내에 존재하는 타겟 타입(예: 아이템, 에너미)의 엔티티들을 최적화된 방식(Subsystem 활용 등)으로 탐지하고, 그 결과를 <종류, 수량> 형태로 반환하여 후속 상호작용을 준비합니다.
    TMap<FString, int32> DetectedEntities;

    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) 
    {
        return DetectedEntities;
    }

    if (TargetEntityType == EEntityType::Item)
    {
        ScanItemsInRange(OwnerCharacter, SearchRadius, DetectedEntities);
    }
    else
    {
        // TODO: 다른 EntityType (Enemy, NPC 등)에 대한 탐지 지원(필요한 경우 추가)
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: 반경 %.1f 내의 엔티티(타입 %d) 탐색 완료. 종류 수: %d"), 
        *GetOwnerAgentID(), SearchRadius, (int32)TargetEntityType, DetectedEntities.Num());

    return DetectedEntities;
}

void UNPCActionComponent::BaseSendEventToActor(AActor* TargetActor, const FString& EventName)
{
    // 플레이어나 타 액터에게 특정 메시지를 던져, 협동이나 대립 같은 복합적인 에코시스템을 유기적으로 연동시키기 위함입니다.
    if (!TargetActor) return;

    // TODO: 인터페이스 통신이나 이벤트 브로드캐스트 구현
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s가 %s에게 이벤트 '%s' 전달"), *GetOwnerAgentID(), *TargetActor->GetName(), *EventName);
}


void UNPCActionComponent::BasePlayActionMedia(const FString& AssetID)
{
    if (!ActionData)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] ActionData Asset 설정 누락!"));
        return;
    }

    if (FActionMediaData* MediaData = ActionData->ActionMedias.Find(AssetID))
    {
        if (MediaData->Montage)
        {
            if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
            {
                OwnerCharacter->PlayAnimMontage(MediaData->Montage);
                UE_LOG(LogTemp, Log, TEXT("[NPCAction] DataAsset 몽타주 재생: %s"), *MediaData->Montage->GetName());
            }
        }
        if (MediaData->Sound)
        {
            // TODO: SoundBase 검색 후 재생
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] DataAsset 사운드 재생: %s"), *MediaData->Sound->GetName());
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 지정한 마스터 에셋 미디어를 찾을 수 없음: %s"), *AssetID);
    }
}

// ----------------------------------------------------------------------------
// [EAction 래퍼 함수 (Action Wrappers)]

FVector UNPCActionComponent::ParseVectorParam(const FString& ParamStr) const
{
    FVector Result = FVector::ZeroVector;
    if (ParamStr.IsEmpty())
    {
        return Result;
    }
    
    // (X=100.000,Y=200.000,Z=300.000) 형태인지 확인하고 파싱
    if (ParamStr.StartsWith(TEXT("(")) && ParamStr.EndsWith(TEXT(")")))
    {
        Result.InitFromString(ParamStr);
    }
    
    return Result;
}

void UNPCActionComponent::ExecuteInteraction(EAction ActionType, AActor* TargetActor, const TMap<FString, FString>& Params)
{
    // [의도(Why)] 다양한 형태(위치, 텍스트, 다중 파라미터)의 JSON 인자를 BTTask 대신 컴포넌트 레벨에서 일괄 파싱 및 캐싱하여 각 세부 Execute 함수로 안전하게 전달합니다. 
    FString TargetID = Params.FindRef(TEXT("TargetID"));
    if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("ItemID")); // Fallback

    FVector Location = ParseVectorParam(Params.FindRef(TEXT("Location")));
    FVector Direction = ParseVectorParam(Params.FindRef(TEXT("Direction")));
    FString TextBody = Params.FindRef(TEXT("DialogueText"));

    // Task, Social, Investigate 특수 파라미터 추출
    FVector StartLocation = ParseVectorParam(Params.FindRef(TEXT("StartLocation")));
    FVector EndLocation = ParseVectorParam(Params.FindRef(TEXT("EndLocation")));
    
    FString GiveItemID = Params.FindRef(TEXT("GiveItemID"));
    int32 GiveAmount = FMath::Max(1, FCString::Atoi(*Params.FindRef(TEXT("GiveAmount"))));
    FString GetItemID = Params.FindRef(TEXT("GetItemID"));
    int32 GetAmount = FMath::Max(1, FCString::Atoi(*Params.FindRef(TEXT("GetAmount"))));
    
    int32 Amount = FMath::Max(1, FCString::Atoi(*Params.FindRef(TEXT("Amount"))));

    TArray<FString> CraftItemIDs;
    FString CraftItemsStr = Params.FindRef(TEXT("ItemIDs"));
    if (CraftItemsStr.IsEmpty()) CraftItemsStr = TargetID;
    CraftItemsStr.ParseIntoArray(CraftItemIDs, TEXT(","), true);

    // 단일화된 EAction enum 값에 따라 세부적인 행동 함수로 라우팅합니다.
    switch (ActionType)
    {
    case EAction::Idle:         ExecuteIdle(); break;
    case EAction::Move:         ExecuteMove(Location, TargetActor); break;
    case EAction::Follow:       ExecuteFollow(TargetActor); break;
    case EAction::Dialogue:     ExecuteDialogue(TextBody, EFacialState::Neutral); break;
    case EAction::TurnTo:       ExecuteTurnTo(Location, TargetActor); break;
    case EAction::Scan:         ExecuteScan(Location, TargetActor); break;
    case EAction::UseItem:      ExecuteUseItem(TargetID); break;
    case EAction::Equip:        ExecuteEquipAction(TargetID); break;
    case EAction::Unequip:      ExecuteUnequipAction(TargetID); break;
    
    // Combat
    case EAction::Attack:       ExecuteAttackAction(TargetActor, EAttackType::Melee); break;
    case EAction::Block:        ExecuteBlock(TargetActor); break;
    case EAction::Dodge:        ExecuteDodgeAction(Direction.IsNearlyZero() ? FVector(100, 100, 0) : Direction); break;
    case EAction::Flee:         ExecuteFlee(Location.IsNearlyZero() && TargetActor ? TargetActor->GetActorLocation() * -1 : Location); break;
    case EAction::SignalAllies: ExecuteSignalAllies(TargetID); break;

    // Social
    case EAction::Trade:        ExecuteTrade(TargetActor, GiveItemID.IsEmpty() ? TargetID : GiveItemID, GiveAmount, GetItemID, GetAmount); break;
    case EAction::GiveItem:     ExecuteGiveItem(TargetActor, TargetID, Amount); break;
    case EAction::Comfort:      ExecuteComfort(TargetActor); break;
    case EAction::HandObject:   ExecuteHandObject(TargetID); break;
    
    // Task
    case EAction::PickUp:       ExecutePickUp(Location); break;
    case EAction::Drop:         ExecuteDrop(TargetID); break;
    case EAction::Craft:        ExecuteCraft(CraftItemIDs); break;
    case EAction::Repair:       ExecuteRepair(TargetID); break;
    
    // Investigate
    case EAction::Investigate:  ExecuteInvestigate(Location); break;
    case EAction::Scout:        ExecuteScout(StartLocation.IsNearlyZero() ? Location : StartLocation, EndLocation.IsNearlyZero() ? Location : EndLocation); break;
    
    // Lifestyle & Social (Emote)
    case EAction::Sit:
    case EAction::Sleep:
    case EAction::Read:
    case EAction::Pray:
    case EAction::Dance:
    case EAction::Sing:
    case EAction::Emote:
        ExecuteLifestyleAction(ActionType, TargetActor, Location, TargetID);
        break;

    default:
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 지원되지 않는 ActionType이 ExecuteInteraction으로 유입됨"));
        break;
    }
}

// ----------------------------------------------------------------------------

// ==========================================
// [1] Common Behaviors
// ==========================================

// [의도(Why)] 위협 상황이나 명령 취소 시, 현재 진행 중인 모든 애니메이션/이동/동작을 강제 리셋하여 대기 상태로 되돌립니다.
void UNPCActionComponent::ExecuteIdle() 
{
    BaseStopCurrentAction();
}

// [Tactical EQS] NPC 스탯/상태를 블랙보드의 EQS 파라미터에 반영합니다.
// ExecuteMove 호출 직전에 자동 실행되어, 타겟 반경/전술 가중치 등을 최신 스탯 기준으로 갱신합니다.
void UNPCActionComponent::UpdateEQSParams()
{
    if (!StateComponent) return;
    ASmartNPCAIController* AICtrl = GetOwnerAIController();
    if (!AICtrl) return;
    UBlackboardComponent* BB = AICtrl->GetBlackboardComponent();
    if (!BB) return;

    const FCharacterAttributes& Attr = StateComponent->GetCurrentStats();

    // [1] Perception -> 탐색 반경 (최대 3000 Clamp)
    float SearchRadius = FMath::Clamp(1000.f + Attr.BaseStats.Perception * 20.f, 500.f, 3000.f);
    BB->SetValueAsFloat(FName("EQS_SearchRadius"), SearchRadius);

    // [2] Fear -> 엄폐 선호도, [3] Feared 상태이상 시 강제 최대화
    float CoverWeight = Attr.Behavior.Fear * 0.02f;
    if (Attr.HasStatusEffect(EStatusEffect::Feared)) CoverWeight = 5.0f;
    BB->SetValueAsFloat(FName("EQS_CoverWeight"), CoverWeight);

    // [4] Intelligence -> 노이즈 감소 (100에 가까울수록 0, 1에 가까울수록 Max 0.5)
    float Intel = FMath::Clamp(static_cast<float>(Attr.BaseStats.Intelligence), 1.f, 100.f);
    BB->SetValueAsFloat(FName("EQS_NoiseWeight"), (100.f - Intel) * 0.005f);

    // [5] Combat.Range -> EQS 안전 거리 기준 (80%가 최적)
    BB->SetValueAsFloat(FName("EQS_SafeDistance"), Attr.Combat.Range * 0.8f);

    UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] EQS Params 갱신 - Radius:%.0f, Cover:%.2f, Noise:%.2f, SafeDist:%.0f"),
        SearchRadius, CoverWeight, (100.f - Intel) * 0.005f, Attr.Combat.Range * 0.8f);
}

// [의도(Why)] 전술 상태(TacticalState)를 결합하여 EQS 에셋을 교체함으로써, 단순 목적지 접근뿐만 아니라 엄폐, 은신, 포위 기동 등의 입체적인 이동을 1개의 함수로 통합 처리합니다.
void UNPCActionComponent::ExecuteMove(FVector TargetLocation, AActor* TargetActor, EMoveType SpeedType, ETacticalMoveState TacticalState)
{
    // [의도(Why)] 안전 거리, 시야, 은폐 성향 등 현재 변경된 동적 스탯을 EQS 쿼리 직전에 밀어넣어 반영합니다.
    UpdateEQSParams();

    UEnvQuery* SelectedQuery = nullptr;

    // TacticalState에 따른 EQS 쿼리 선택
    switch (TacticalState)
    {
    case ETacticalMoveState::Cover:      SelectedQuery = CoverFinderQuery; break;
    case ETacticalMoveState::Flanking:   SelectedQuery = FlankingQuery;    break;
    case ETacticalMoveState::Retreat:    SelectedQuery = RetreatQuery;     break;
    default:                             SelectedQuery = DefaultMoveQuery;  break;
    }

    // Default + 원거리 무기일 때 원거리 쿼리로 자동 전환
    if (TacticalState == ETacticalMoveState::Default && StateComponent)
    {
        if (StateComponent->GetCurrentStats().Combat.Range > 500.f && RangedOptimalPositionQuery)
        {
            SelectedQuery = RangedOptimalPositionQuery;
        }
    }

    if (SelectedQuery)
    {
        FEnvQueryRequest QueryRequest(SelectedQuery, GetOwner());
        QueryRequest.Execute(EEnvQueryRunMode::SingleResult,
            this, &UNPCActionComponent::OnTacticalMoveCompleted);
        return; // EQS 콜백에서 처리
    }

    // EQS 에셋 할당 없음 -> 기존 Fallback 직접 이동
    const FVector MoveTarget = TargetActor ? TargetActor->GetActorLocation() : TargetLocation;
    BaseMove(MoveTarget, SpeedType);
}

// EQS 쿼리 콜백: 결과 좌표로 실제 이동 명령을 수행합니다.
void UNPCActionComponent::OnTacticalMoveCompleted(TSharedPtr<FEnvQueryResult> Result)
{
    if (!Result || !Result->IsSuccessful())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] EQS 쿼리 실패 - Fallback 없음. 이동 취소."));
        return;
    }
    FVector BestLocation = Result->GetItemAsLocation(0);
    BaseMove(BestLocation, EMoveType::Run);
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] EQS 전술이동 -> X=%.1f Y=%.1f Z=%.1f"),
        BestLocation.X, BestLocation.Y, BestLocation.Z);
}

// 지정된 타겟 액터를 일정 간격을 두고 따라다닙니다. 호위나 감시 등의 상황에 유용합니다.
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

// 대상과 텍스트 형태의 대화를 시작하며, 대화 내용의 뉘앙스에 맞는 표정을 출력합니다.
void UNPCActionComponent::ExecuteDialogue(const FString& DialogueText, const EFacialState Emotion)
{
    BaseDialogue(DialogueText, Emotion);
}

// 특정 위치나 타겟(우선)을 바라보도록 몸을 부드럽게 회전시켜, 상호작용 의지나 집중을 시각적으로 나타냅니다.
void UNPCActionComponent::ExecuteTurnTo(FVector TargetLocation, AActor* TargetActor) 
{
    FVector FocusLocation = TargetActor ? TargetActor->GetActorLocation() : TargetLocation;
    BaseFaceRotate(FocusLocation);
}

// 주변을 무작위로 둘러보며 경계하거나 탐색하는 행동입니다. 타겟이 있다면 타겟 주변을 확인합니다.
void UNPCActionComponent::ExecuteScan(FVector TargetLocation, AActor* TargetActor) 
{
    FVector FocusLocation = TargetActor ? TargetActor->GetActorLocation() : TargetLocation;
    FVector RandomOffset = FVector(FMath::VRand().X, FMath::VRand().Y, 0.0f) * 100.0f;
    BaseFaceRotate(FocusLocation + RandomOffset, 3.0f);
}

// 인벤토리에서 지정된 소모성 아이템(예: 음식, 물약)을 꺼내 소비합니다.
void UNPCActionComponent::ExecuteUseItem(const FString& ItemID) 
{
    if (InventoryComponent && InventoryComponent->RemoveItem(ItemID, 1))
    {
        BasePlayActionMedia(TEXT("Eat")); // 차후 ItemID에 따른 몽타주 연동 기능으로 고도화 가능
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 사용(소모): %s"), *ItemID);

        // 아이템 소모/조작 시 작은 소음 발생
        if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
        {
            UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_UseItem, OwnerCharacter, 0.0f, NPCActionKeys::NoiseTag_UseItem);
        }
    }
}

// 인벤토리에 보유 중인 지정된 장비 아이템을 몸에 착용하거나 손에 장비합니다.
void UNPCActionComponent::ExecuteEquipAction(const FString& ItemID) 
{
    if (InventoryComponent)
    {
        InventoryComponent->EquipItem(ItemID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 장착: %s"), *ItemID);
    }
}

// 현재 착용/장착 중인 아이템을 해제하여 다시 가방(인벤토리)에 넣습니다.
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

// 대상을 향해 전투 행위를 수행합니다. 타겟을 바라보며 접근한 뒤, 공격 몽타주와 판정을 발생시킵니다.
void UNPCActionComponent::ExecuteAttackAction(AActor* TargetActor, EAttackType AttackType) 
{
    if (TargetActor)
    {
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
        BaseMove(TargetActor->GetActorLocation(), EMoveType::Run);
        BasePlayActionMedia(TEXT("Attack")); // 기본 공격 몽타주 재생 (추후 무기 타입별 확장 가능)

        // 공격 액션 시 주변에 큰 소음 발생
        if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
        {
            UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_Attack, OwnerCharacter, 0.0f, NPCActionKeys::NoiseTag_Attack);
        }
    }
}

// 타겟의 물리적 공격에 대비하여 방어 자세를 취하고 대미지 감소를 도모합니다.
void UNPCActionComponent::ExecuteBlock(AActor* TargetActor) 
{
    if (TargetActor)
    {
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    }
    BasePlayActionMedia(TEXT("Block")); // 방어 몽타주 재생
}

// 들어오는 피격판정을 회피하기 위해 지정된 방향(안전지대)으로 기민하게 움직입니다.
void UNPCActionComponent::ExecuteDodgeAction(FVector Direction) 
{
    // [설명] 단순 제자리 회피 애니메이션을 재생하는 방식.
    // 더 정교한 회피는 Direction 방향에 맞춰 RootMotion이나 Impulse를 줄 수도 있습니다.
    ExecuteTurnTo(GetOwner()->GetActorLocation() + Direction * 100.0f, nullptr);
    BasePlayActionMedia(TEXT("Dodge")); 
}

// 심각한 위협으로부터 벗어나기 위해 다급히 달아납니다. (Run 스피드 강제 적용)
void UNPCActionComponent::ExecuteFlee(FVector EscapeLocation) 
{
    BaseMove(EscapeLocation, EMoveType::Run);
}

// 아군에게 시각적 수신호를 보내어 전투 상황, 대기, 돌격 등을 지시합니다.
void UNPCActionComponent::ExecuteSignalAllies(const FString& HandSign) 
{
    BaseSignalAllies(HandSign);
}

// ==========================================
// [3] Social Behaviors
// ==========================================

// [의도(Why)] 다른 캐릭터와 물물교환을 시도하여 경제/사교적 상호작용의 기반을 마련합니다.
void UNPCActionComponent::ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, int32 GiveAmount, const FString& GetItemID, int32 GetAmount) 
{
    // TODO: 인벤토리 아이템 GetItemCount로 개수 확인 후 GiveItem 실행
    ExecuteGiveItem(TargetActor, GiveItemID, GiveAmount);
}

// [의도(Why)] 대가 없이 아이템을 건네주어 호감도 상승이나 퀘스트 이벤트를 성립시킵니다.
void UNPCActionComponent::ExecuteGiveItem(AActor* TargetActor, const FString& ItemID, int32 Amount) 
{
    if (InventoryComponent && InventoryComponent->HasItem(ItemID, Amount))
    {
        InventoryComponent->RemoveItem(ItemID, Amount);
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
        BasePlayActionMedia(TEXT("Give"));
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 전달 완료: %s"), *ItemID);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCAction] 아이템 없음: %s"), *ItemID);
    }
}

// 불안해하거나 상처받은 대상을 다독여 정신적 상태를 회복시킵니다.
void UNPCActionComponent::ExecuteComfort(AActor* TargetActor) 
{
    BaseComfort(TargetActor);
}

// 가방에서 물건을 잠시 손에 들고 대상에게 무언가를 보여주거나 설명합니다.
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

// [의도(Why)] 월드에 존재하는 물리적 아이템을 인벤토리로 수거하여 자원 시스템과 연동합니다.
void UNPCActionComponent::ExecutePickUp(FVector Location) 
{
    // 타겟 위치로 걷기 지시 후 줍기 전용 애니메이션을 재생하여 몰입감을 높입니다.
    BaseMove(Location, EMoveType::Walk);
    BasePlayActionMedia(TEXT("PickUp"));

    // 반경 내에 존재하는 아이템 엔티티 목록을 맵 형태로(ID -> 누적수량) 추출합니다.
    const TMap<FString, int32> DetectedEntities = BaseDetectEntityInRange(100.0f, EEntityType::Item);
    UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    UItemManager* ItemManager = GameInstance ? GameInstance->GetSubsystem<UItemManager>() : nullptr;

    // 핵심 매니저 및 인벤토리 의존성 부재 시 시스템 크래시를 막기 위한 조기 반환(Early Return)입니다.
    if (!ItemManager || !InventoryComponent) return;

    // 탐지된 아이템 고유 ID(Key)와 누적 수량(Value)을 순회하며 인벤토리에 추가를 시도합니다.
    for (const auto& Pair : DetectedEntities)
    {
        const FString& ItemID = Pair.Key;
        const int32 Amount = Pair.Value;
        FItemData FoundData;
        if (ItemManager->GetItemDataByID(ItemID, FoundData))
        {
            InventoryComponent->AddItem(FoundData, Amount);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 %d개 줍기 성공: %s"), Amount, *ItemID);
        }
        else
        {
            // 에코시스템 예외 처리: 데이터 규격이 맞지 않는 유령 아이템 파악용 로그
            UE_LOG(LogTemp, Error, TEXT("[NPCAction] 데이터 테이블에서 정보를 찾을 수 없어 줍기 실패 (에셋 확인 요망): %s"), *ItemID);
        }
    }
}

void UNPCActionComponent::ExecuteDrop(const FString& TargetTemplateID) 
{
    // NPC가 소지한 특정 아이템을 월드에 스폰(Drop)하고, 이 액터를 ItemManager에 글로벌 추적 대상으로 즉시 등록합니다.
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter || !InventoryComponent) 
    {
        return;
    }

    const int32 DropQuantity = 1;
    if (InventoryComponent->RemoveItem(TargetTemplateID, DropQuantity))
    {
        BasePlayActionMedia(TEXT("Drop"));
        
        // 물건을 떨어뜨릴 때 소음 발생
        UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_Drop, OwnerCharacter, 0.0f);

        UGameInstance* GameInstance = OwnerCharacter->GetGameInstance();
        UWorld* World = OwnerCharacter->GetWorld();
        if (IsValid(GameInstance) && IsValid(World))
        {
            if (UItemManager* ItemManager = GameInstance->GetSubsystem<UItemManager>())
            {
                // NPC의 전방 약간 떨어진 곳(100.0f)에 아이템 액터 스폰을 유도합니다.
                const FVector DropLocation = OwnerCharacter->GetActorLocation() + (OwnerCharacter->GetActorForwardVector() * 100.0f);
                const FRotator DropRotation = FRotator::ZeroRotator;
                
                // TODO: TargetTemplateID를 기반으로 실제 매칭되는 AActor 블루프린트 클래스를 찾아와 스폰해야 합니다.
                AActor* SpawnedItemActor = nullptr; 
                // SpawnedItemActor = World->SpawnActor<AActor>(ItemBlueprintClass, DropLocation, DropRotation);

                if (IsValid(SpawnedItemActor))
                {
                    // 월드 스폰 성공 시 중복되지 않는 고유 UUID를 즉석 생성하여 매니저에 관리 권한을 넘깁니다.
                    const FString NewInstanceUUID = FGuid::NewGuid().ToString();
                    ItemManager->RegisterDroppedItem(NewInstanceUUID, SpawnedItemActor, TargetTemplateID);
                    
                    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: 아이템 드랍 및 매니저 등록 성공. TemplateID: %s, UID: %s"), 
                           *GetOwnerAgentID(), *TargetTemplateID, *NewInstanceUUID);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("[NPCAction] %s: 인벤토리 차감은 성공하나 실제 스폰 누락(TODO). TemplateID: %s"), 
                           *GetOwnerAgentID(), *TargetTemplateID);
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] %s: 아이템 버리기 실패 (인벤토리에 없음). TemplateID: %s"), 
               *GetOwnerAgentID(), *TargetTemplateID);
    }
}

// [의도(Why)] 다중 재료를 소모해 결과물(아이템)을 합성/제작하는 생산 시스템의 엔드포인트입니다.
void UNPCActionComponent::ExecuteCraft(const TArray<FString>& ItemIDs) 
{
    // TODO: 제작 레시피 검증 및 아이템 소모/생성 로직 연동
    BasePlayActionMedia(TEXT("Craft"));
}

// [의도(Why)] 파손된 객체나 장비의 내구도를 회복시켜 유지보수 관련 태스크 행동을 구현합니다.
void UNPCActionComponent::ExecuteRepair(const FString& ItemID) 
{
    BasePlayActionMedia(TEXT("Repair"));
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 수리 완료: %s"), *ItemID);
    if (InventoryComponent && InventoryComponent->HasItem(ItemID))
    {
        float RepairAmount = StateComponent ? StateComponent->GetCurrentStats().BaseStats.Perception : 10.0f;
        InventoryComponent->RepairItem(ItemID, RepairAmount);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 아이템 수리 실패 (미보유): %s"), *ItemID);
    }
}

// ==========================================
// [5] Investigation Behaviors
// ==========================================

// [의도(Why)] 소음/단서가 발생한 곳으로 접근한 후 주변 환경을 스캔하여 은닉된 위협을 능동적으로 찾아냅니다.
void UNPCActionComponent::ExecuteInvestigate(FVector Location) 
{
    BaseMove(Location, EMoveType::Walk);
    ExecuteScan(Location, nullptr); // 주변 둘러보기 연계
}

// 두 지정된 지점 사이를 정찰하며 위협 요소를 파악합니다.
// [의도(Why)] 현재 위치에서 가장 가까운 정찰 지점을 파악하여 이동시킴으로써 비합리적인 동선을 방지합니다. 왕복 순찰 루프는 상위 BT에서 제어함을 전제로 합니다.
void UNPCActionComponent::ExecuteScout(FVector StartLocation, FVector EndLocation) 
{
    FVector TargetDestination = EndLocation;

    if (AActor* OwnerActor = GetOwner())
    {
        FVector CurrentLoc = OwnerActor->GetActorLocation();
        float DistToStart = FVector::DistSquared(CurrentLoc, StartLocation);
        float DistToEnd = FVector::DistSquared(CurrentLoc, EndLocation);
        
        TargetDestination = (DistToStart < DistToEnd) ? StartLocation : EndLocation;
    }
    // TODO::나중엔 BehaviorTree의 Loop 구조와 결합해 왕복하게끔 설계해야 함
    BaseMove(TargetDestination, EMoveType::Walk);
}

// ==========================================
// [6] Lifestyle Behaviors
// ==========================================

// [의도(Why)] 기존에 파편화되어 있던 앉기, 자기, 기도, 춤, 노래, 감정표현 등의 라이프스타일 액션들을
// 하나의 라우팅 함수로 통합하여 코드 중복을 제거하고 유지보수성을 극대화합니다.
void UNPCActionComponent::ExecuteLifestyleAction(EAction LifestyleType, AActor* TargetEntity, FVector Location, const FString& StringParam)
{
    // 1. 공통 이동 및 회전 보간 처리 (이동 필요 여부에 따라)
    FVector Dest = TargetEntity ? TargetEntity->GetActorLocation() : Location;
    if (!Dest.IsNearlyZero())
    {
        // 걷기로 느긋하게 이동 후 대상을 바라봅니다.
        BaseMove(Dest, EMoveType::Walk);
        ExecuteTurnTo(Dest, TargetEntity);
    }

    // 2. 타입에 따른 미디어(몽타주/애니) 처리 분기
    switch (LifestyleType)
    {
        case EAction::Sit:   BaseSitDown(TargetEntity); break;
        case EAction::Sleep: BaseLieDown(TargetEntity); break;
        case EAction::Pray:  BasePlayActionMedia(TEXT("Pray")); break;
        case EAction::Read:  BasePlayActionMedia(TEXT("Read")); break;
        case EAction::Dance: BaseDance(StringParam); break;
        case EAction::Sing:  BaseSing(StringParam); break;
        case EAction::Emote: BaseEmote(StringParam); break;
        // Clean 등 추가 확장이 필요하다면 여기에 분기를 추가합니다.
        default: break;
    }
}

