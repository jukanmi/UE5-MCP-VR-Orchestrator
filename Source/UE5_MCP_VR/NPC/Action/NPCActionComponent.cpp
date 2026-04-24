#include "NPCActionComponent.h"
#include "Engine/OverlapResult.h"
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
#include "../../Network/EnvelopeBuilder.h"
#include "../NPCManager.h"
#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#endif

namespace
{
    FName GetGameplayTagForAction(EAction ActionType)
    {
        switch(ActionType)
        {
            // Common
            case EAction::Move:
            case EAction::Follow:
                return FName("State.Action.Common.Move");
            case EAction::TurnTo:
                return FName("State.Action.Common.TurnTo");
            case EAction::Scan:
                return FName("State.Action.Common.Scan");
            case EAction::UseItem:
                return FName("State.Action.Common.UseItem");
            case EAction::Equip:
                return FName("State.Action.Common.Equip");
            case EAction::Unequip:
                return FName("State.Action.Common.Unequip");
            case EAction::Dialogue:
                return FName("State.Action.Common.Dialogue");

            // Combat
            case EAction::Attack:
                return FName("State.Action.Combat.Attack");
            case EAction::Block:
                return FName("State.Action.Combat.Block");
            case EAction::Dodge:
                return FName("State.Action.Combat.Dodge");
            case EAction::Flee:
                return FName("State.Action.Combat.Flee");
            case EAction::SignalAllies:
                return FName("State.Action.Combat.SignalAllies");

            // Social
            case EAction::Emote:
                return FName("State.Action.Social.Emote");
            case EAction::Trade:
                return FName("State.Action.Social.Trade");
            case EAction::GiveItem:
                return FName("State.Action.Social.GiveItem");
            case EAction::Comfort:
                return FName("State.Action.Social.Comfort");
            case EAction::HandObject:
                return FName("State.Action.Social.HandObject");
            case EAction::Dance:
                return FName("State.Action.Social.Dance");
            case EAction::Sing:
                return FName("State.Action.Social.Sing");

            // Task
            case EAction::PickUp:
                return FName("State.Action.Task.PickUp");
            case EAction::Drop:
                return FName("State.Action.Task.Drop");
            case EAction::Repair:
                return FName("State.Action.Task.Repair");

            // Investigation
            case EAction::Investigate:
                return FName("State.Action.Investigation.Investigate");

            default:
                return NAME_None;
        }
    }

    void TryParseAndSetTargetLocation(UBlackboardComponent* BB, const FString& TargetLocStr)
    {
        if (TargetLocStr.IsEmpty()) return;
        FVector Loc;
        if (Loc.InitFromString(TargetLocStr))
        {
            BB->SetValueAsVector(ASmartNPCAIController::Key_TargetLocation, Loc);
        }
    }

    void ResetAllStateTagsToIdle(AActor* Target)
    {
        if (ASmartNPC* NPC = Cast<ASmartNPC>(Target))
        {
            NPC->GameplayTags.Reset();
            NPC->AddStateTag(FGameplayTag::RequestGameplayTag(FName("State.Idle")));
        }
    }

    void TransitionStateTag(AActor* Target, EAction ActionType)
    {
        if (ASmartNPC* NPC = Cast<ASmartNPC>(Target))
        {
            NPC->RemoveStateTag(FGameplayTag::RequestGameplayTag(FName("State.Idle")));
            FName ActionTagName = GetGameplayTagForAction(ActionType);
            if (!ActionTagName.IsNone())
            {
                NPC->AddStateTag(FGameplayTag::RequestGameplayTag(ActionTagName));
            }
        }
    }

    void RevertStateTagToIdle(AActor* Target, EAction ActionType)
    {
        if (ASmartNPC* NPC = Cast<ASmartNPC>(Target))
        {
            FName ActionTagName = GetGameplayTagForAction(ActionType);
            if (!ActionTagName.IsNone())
            {
                NPC->RemoveStateTag(FGameplayTag::RequestGameplayTag(ActionTagName));
            }
            NPC->AddStateTag(FGameplayTag::RequestGameplayTag(FName("State.Idle")));
        }
    }

    void ScanItemsInRange(AActor* OwnerActor, float SearchRadius, TMap<FString, int32>& OutEntities)
    {
        if (!OwnerActor) return;
        TArray<FOverlapResult> Overlaps;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(OwnerActor);
        bool bHit = OwnerActor->GetWorld()->OverlapMultiByObjectType(
            Overlaps,
            OwnerActor->GetActorLocation(),
            FQuat::Identity,
            FCollisionObjectQueryParams(ECC_PhysicsBody),
            FCollisionShape::MakeSphere(SearchRadius),
            Params
        );
        if (bHit)
        {
            for (auto& Result : Overlaps)
            {
                if (AActor* HitActor = Result.GetActor())
                {
                    if (HitActor->GetClass()->ImplementsInterface(UItem::StaticClass()))
                    {
                        FString ItemID = IItem::Execute_GetItemID(HitActor);
                        OutEntities.FindOrAdd(ItemID, 0)++;
                    }
                }
            }
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
    if (!StateComponent) return 200.f;
    const auto& M = StateComponent->GetAttributes().Movement;
    switch (Type)
    {
    case EMoveType::Run:    return M.RunSpeed;
    case EMoveType::Sprint: return M.SprintSpeed;
    case EMoveType::Crouch: return M.CrouchSpeed;
    default:                return M.WalkSpeed;
    }
}

// === Action Batch System ===


void UNPCActionComponent::ExecuteActionBatch(const FActionBatch& Batch)
{
    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
        {
            BB->SetValueAsEnum(ASmartNPCAIController::Key_BehaviorMode, (uint8)Batch.Mode);
            // [상세 로그] 서버로부터 수신된 리액션 덤프
            UE_LOG(LogTemp, Warning, TEXT("=================================================="));
            UE_LOG(LogTemp, Warning, TEXT("[ACTION RECEIVED] NPC: %s | Mode: %s"), 
                *GetOwnerAgentID(), *UEnum::GetValueAsString(Batch.Mode));
            
            for (int32 i = 0; i < Batch.Actions.Num(); ++i)
            {
                const FGameAction& Action = Batch.Actions[i];
                FString ParamStr;
                for (auto& Pair : Action.Parameters)
                {
                    ParamStr += FString::Printf(TEXT("%s=%s, "), *Pair.Key, *Pair.Value);
                }
                
                UE_LOG(LogTemp, Warning, TEXT("  Action[%d]: %s (Facial: %s)"), 
                    i, *UEnum::GetValueAsString(Action.ActionType), *UEnum::GetValueAsString(Action.FacialState));
                if (!ParamStr.IsEmpty())
                {
                    UE_LOG(LogTemp, Warning, TEXT("    - Params: %s"), *ParamStr);
                }
            }
            UE_LOG(LogTemp, Warning, TEXT("=================================================="));

        }
    }

    // 새 배치 수신 시 대화 슬롯 해제 → 이전 배치의 bIsDialogueActive=true 고착 방지
    bIsDialogueActive = false;

    // [의도(Why)] 대화 액션은 이동 등의 물리적인 행동과 병렬 실행(단기 병렬 큐)되어야 하므로 따로 처리하고, 나머지는 물리 액션 큐에 순차 적재합니다.
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
    // Queue 처리 시작을 BT에게 위임 (HasAction 플래그 세팅)
    if (!ActionQueue.IsEmpty())
    {
        if (ASmartNPCAIController* AI = GetOwnerAIController())
        {
            if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
            {
                BB->SetValueAsBool(ASmartNPCAIController::Key_HasAction, true);
            }
        }
    }
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

bool UNPCActionComponent::ProcessNextAction()
{
    if (bIsBusy || ActionQueue.IsEmpty()) return false;

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
        return true;
    }
    return false;
}

void UNPCActionComponent::OnActionCompleted()
{
    bIsBusy = false;
    
    EAction CompletedAction = StateComponent ? StateComponent->GetCurrentActionType() : EAction::Idle;
    FString CompletedActionStr = UEnum::GetValueAsString(CompletedAction);

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Action '%s' Completed."), *GetOwnerAgentID(), *CompletedActionStr);

    RevertStateTagToIdle(GetOwner(), CompletedAction);

    if (StateComponent) StateComponent->SetCurrentActionType(EAction::Idle);

    // 다음 큐 항목 처리는 BTTask_PrepareNextAction이 주도함
    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        if (UBlackboardComponent* BB = AI->GetBlackboardComponent())
        {
            BB->SetValueAsBool(ASmartNPCAIController::Key_HasAction, !ActionQueue.IsEmpty());
        }
    }
}

void UNPCActionComponent::AbortCurrentAction()
{

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
    if (StateComponent) StateComponent->SetFacialExpression(Emotion);
}

void UNPCActionComponent::BaseDialogue(const FString& DialogueText, const EFacialState Emotion)
{
    BaseEmotion(Emotion);
    if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
        UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_Dialogue, OwnerCharacter, 0.0f, NPCActionKeys::NoiseTag_Dialogue);
    OnNPCDialogue.Broadcast(GetOwnerAgentID(), DialogueText);
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s 대화: %s"), *GetOwnerAgentID(), *DialogueText);
}

void UNPCActionComponent::BaseFaceRotate(FVector TargetLocation, float TurnSpeed)
{
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;
    if (AAIController* AIController = Cast<AAIController>(OwnerCharacter->GetController()))
        AIController->SetFocalPoint(TargetLocation);
}

void UNPCActionComponent::BaseSitDown(AActor* TargetSeat)
{
    if (StateComponent) StateComponent->bIsSit = true;
    BasePlayActionMedia(NPCActionKeys::Interact_SitDown);
}

void UNPCActionComponent::BaseSitUp()
{
    if (StateComponent) StateComponent->bIsSit = false;
    BasePlayActionMedia(NPCActionKeys::Interact_SitUp);
}

void UNPCActionComponent::BaseLieDown(AActor* TargetBed)
{
    if (StateComponent) StateComponent->bIsLie = true;
    BasePlayActionMedia(NPCActionKeys::Interact_LieDown);
}

void UNPCActionComponent::BaseLieUp()
{
    if (StateComponent) StateComponent->bIsLie = false;
    BasePlayActionMedia(NPCActionKeys::Interact_LieUp);
}

void UNPCActionComponent::BaseStopCurrentAction() { AbortCurrentAction(); }

void UNPCActionComponent::BaseSignalAllies(const FString& SignAssetID) { BasePlayActionMedia(SignAssetID); }

void UNPCActionComponent::BaseComfort(AActor* TargetActor)
{
    if (TargetActor) ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    BasePlayActionMedia(TEXT("Comfort"));
}

void UNPCActionComponent::BaseEmote(const FString& EmoteAssetID) { BasePlayActionMedia(EmoteAssetID); }
void UNPCActionComponent::BaseDance(const FString& DanceAssetID) { BasePlayActionMedia(DanceAssetID); }
void UNPCActionComponent::BaseSing(const FString& SingAssetID)   { BasePlayActionMedia(SingAssetID); }

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
    if (!ParamStr.IsEmpty() && ParamStr.StartsWith(TEXT("(")) && ParamStr.EndsWith(TEXT(")")))
        Result.InitFromString(ParamStr);
    return Result;
}

void UNPCActionComponent::ExecuteInteraction(EAction ActionType, AActor* TargetActor, const TMap<FString, FString>& Params)
{
    // [의도(Why)] 다양한 형태(위치, 텍스트, 다중 파라미터)의 JSON 인자를 BTTask 대신 컴포넌트 레벨에서 일괄 파싱 및 캐싱하여 각 세부 Execute 함수로 안전하게 전달합니다. 
    FString TargetID = Params.FindRef(TEXT("TargetID"));
    if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("ItemID")); // Fallback

    // "Location" 키 우선, 전술 이동 큐 주입 시 사용하는 "TargetLoc" 키도 폴백으로 지원
    FVector Location = ParseVectorParam(Params.FindRef(TEXT("Location")));
    if (Location.IsZero()) Location = ParseVectorParam(Params.FindRef(TEXT("TargetLoc")));
    FVector Direction = ParseVectorParam(Params.FindRef(TEXT("Direction")));
    FString TextBody = Params.FindRef(NPCActionKeys::Key_Text);

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

    case EAction::Wait:
        // BT Task가 ExecuteInteraction 직후 OnActionCompleted를 호출하므로 여기선 아무것도 하지 않음
        // duration 기반 실제 대기가 필요하다면 BTTask_ExecuteSmartAction을 LatentTask로 전환해야 함
        break;

    default:
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 지원되지 않는 ActionType이 ExecuteInteraction으로 유입됨: %s"),
            *UEnum::GetValueAsString(ActionType));
        break;
    }
}

// ----------------------------------------------------------------------------



void UNPCActionComponent::DrawEQSCandidates(const TArray<FLocationCandidate>& Candidates, float Duration) const
{
#if !UE_BUILD_SHIPPING
    if (!bEQSDebugDraw || !GetWorld()) return;

    for (const FLocationCandidate& C : Candidates)
    {
        FColor SphereColor = FColor::Yellow;
        if (C.Category == ELocationCategory::Safe)       SphereColor = FColor::Green;
        else if (C.Category == ELocationCategory::Aggressive) SphereColor = FColor::Red;

        // 구체 렌더링 (반지름 40)
        DrawDebugSphere(GetWorld(), C.Location, 40.f, 12, SphereColor, false, Duration);

        // 점수 + ID 텍스트
        const FString Label = FString::Printf(
            TEXT("[%s]\nScore:%.2f\nDist:%.0f\nCover:%.1f"),
            *C.CandidateId, C.Score, C.DistanceToEnemy, C.CoverRating);
        DrawDebugString(GetWorld(), C.Location + FVector(0, 0, 80.f), Label, nullptr, SphereColor, Duration);
    }
#endif
}

void UNPCActionComponent::DrawEQSChosenLocation(const FVector& Loc, const FString& CandidateId, const FString& Reason, float Duration) const
{
#if !UE_BUILD_SHIPPING
    if (!bEQSDebugDraw || !GetWorld()) return;

    DrawDebugSphere(GetWorld(), Loc, 80.f, 16, FColor::Cyan, false, Duration);

    FString Label = FString::Printf(TEXT("★ CHOSEN: %s\n(%.0f, %.0f, %.0f)"), *CandidateId, Loc.X, Loc.Y, Loc.Z);
    if (!Reason.IsEmpty())
        Label += FString::Printf(TEXT("\n%s"), *Reason);

    DrawDebugString(GetWorld(), Loc + FVector(0, 0, 120.f), Label, nullptr, FColor::White, Duration);
#endif
}
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

    const FNPCAttributes& Attr = StateComponent->GetAttributes();

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
        if (StateComponent->GetAttributes().Combat.Range > 500.f && RangedOptimalPositionQuery)
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
    
    // [추가] 시각화 & 구조화 로그
    DrawEQSChosenLocation(BestLocation, TEXT("SingleResult"));
    UE_LOG(LogTemp, Log,
        TEXT("[EQS RESULT] AgentID=%s | Mode=SingleResult | X=%.1f Y=%.1f Z=%.1f"),
        *GetOwnerAgentID(), BestLocation.X, BestLocation.Y, BestLocation.Z);
}

// ============================================================================
// [전술 위치 결정 파이프라인] EQS AllMatching → 스코어링 → LLM 전송
// ============================================================================

namespace
{
    /** 후보 위치에서 가장 가까운 적까지의 거리 반환. 적 없으면 FLT_MAX */
    float CalcDistToNearestEnemy(const FVector& Loc, const TArray<FVector>& Enemies)
    {
        float MinDist = FLT_MAX;
        for (const FVector& E : Enemies)
            MinDist = FMath::Min(MinDist, FVector::Dist(Loc, E));
        return MinDist;
    }

    /** 위치에서 적 방향으로 시야 체크 (블로킹 있으면 엄폐 = 1.0) */
    float CalcCoverRating(const FVector& Loc, const TArray<FVector>& Enemies, UWorld* World, const AActor* Querier)
    {
        if (Enemies.IsEmpty() || !World) return 0.f;
        int32 BlockedCount = 0;
        FCollisionQueryParams Params(NAME_None, false, Querier);
        for (const FVector& E : Enemies)
        {
            FHitResult Hit;
            const FVector Start = Loc + FVector(0, 0, 60.f);
            const FVector End   = E  + FVector(0, 0, 60.f);
            if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
                && Hit.GetActor() && Hit.GetActor() != Querier)
            {
                ++BlockedCount;
            }
        }
        return static_cast<float>(BlockedCount) / Enemies.Num();
    }

    /** Safe 스코어: 멀수록, 엄폐할수록, HP 낮을수록 가중치 */
    float EvalSafeScore(const FVector& Loc, const TArray<FVector>& Enemies, UWorld* World, const AActor* Querier, float HpPct)
    {
        const float Dist  = CalcDistToNearestEnemy(Loc, Enemies);
        const float Cover = CalcCoverRating(Loc, Enemies, World, Querier);
        // 적이 없으면 LOS 없음 → penalty 없음
        const bool bLOS   = (Cover < 0.5f);
        float Score = FMath::Min(Dist / 1500.f, 1.f) * 3.f;  // 거리: 0~3
        Score += Cover * 2.f;                                  // 엄폐: 0~2
        Score += bLOS ? -1.f : 0.f;                           // LOS 노출 패널티
        Score += (1.f - HpPct) * 1.5f;                        // HP 낮을수록 도주 보너스
        return Score;
    }

    /** Aggressive 스코어: 가까울수록, LOS 있을수록 */
    float EvalAggressiveScore(const FVector& Loc, const TArray<FVector>& Enemies, UWorld* World, const AActor* Querier, float HpPct)
    {
        const float Dist  = CalcDistToNearestEnemy(Loc, Enemies);
        const float Cover = CalcCoverRating(Loc, Enemies, World, Querier);
        const bool bLOS   = (Cover < 0.5f);
        float Score = (1.f - FMath::Min(Dist / 1500.f, 1.f)) * 3.f; // 가까울수록 ↑
        Score += bLOS ? 2.f : 0.f;                                    // LOS: 적 보여야 공격 가능
        Score += Cover * -0.5f;                                        // 엄폐는 소폭 페널티
        Score += HpPct * 1.f;                                          // HP 높을수록 공격 선호
        return Score;
    }

    /** Optimal 스코어: 중간 거리 + LOS + 적당한 엄폐 */
    float EvalOptimalScore(const FVector& Loc, const TArray<FVector>& Enemies, UWorld* World, const AActor* Querier, float)
    {
        const float Dist  = CalcDistToNearestEnemy(Loc, Enemies);
        const float Cover = CalcCoverRating(Loc, Enemies, World, Querier);
        const bool bLOS   = (Cover < 0.5f);
        // 600~1000 구간 선호 (정규화 곡선 대신 단순 선형 피크)
        const float IdealDist = 800.f;
        const float DistScore = FMath::Max(0.f, 2.f - FMath::Abs(Dist - IdealDist) / 800.f);
        return DistScore + Cover * 1.f + (bLOS ? 1.f : 0.f);
    }

    FString CategoryToString(ELocationCategory Cat)
    {
        switch (Cat)
        {
        case ELocationCategory::Safe:       return TEXT("SAFE");
        case ELocationCategory::Aggressive: return TEXT("AGGRESSIVE");
        default:                            return TEXT("OPTIMAL");
        }
    }
}

void UNPCActionComponent::StartTacticalQuery(const TArray<FVector>& EnemyLocations)
{
    if (TacticalQueryState != ETacticalQueryState::Idle)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] StartTacticalQuery 호출 무시 - 이미 진행 중 (%d)"),
            static_cast<int32>(TacticalQueryState));
        return;
    }

    CachedEnemyLocations = EnemyLocations;
    TacticalCandidateMap.Empty();

    UEnvQuery* QueryAsset = TacticalPositionsQuery ? TacticalPositionsQuery : DefaultMoveQuery;
    if (!QueryAsset)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] StartTacticalQuery - EQS 에셋 없음 → Failed"));
        TacticalQueryState = ETacticalQueryState::Failed;
        return;
    }

    UpdateEQSParams();
    TacticalQueryState = ETacticalQueryState::WaitingEQS;

    // AllMatching: 복수 후보 전부 반환
    FEnvQueryRequest QueryRequest(QueryAsset, GetOwner());
    QueryRequest.Execute(EEnvQueryRunMode::AllMatching,
        this, &UNPCActionComponent::OnTacticalCandidatesDone);

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - 전술 EQS 쿼리 시작 (적 %d명)"),
        *GetOwnerAgentID(), EnemyLocations.Num());
}

void UNPCActionComponent::OnTacticalCandidatesDone(TSharedPtr<FEnvQueryResult> Result)
{
    if (!Result || !Result->IsSuccessful() || Result->Items.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 전술 EQS 결과 없음 → Failed"));
        TacticalQueryState = ETacticalQueryState::Failed;
        return;
    }

    UWorld* World = GetWorld();
    AActor* Owner = GetOwner();
    const float HpPct = StateComponent
        ? StateComponent->GetAttributes().Resources.GetHealthPercent()
        : 0.5f;

    // ── 스코어링 ─────────────────────────────────────────────────────────────
    TArray<FLocationCandidate> AllCandidates;
    AllCandidates.Reserve(Result->Items.Num());

    for (int32 i = 0; i < Result->Items.Num(); ++i)
    {
        const FVector Loc = Result->GetItemAsLocation(i);

        const float SafeScore  = EvalSafeScore(Loc, CachedEnemyLocations, World, Owner, HpPct);
        const float AggrScore  = EvalAggressiveScore(Loc, CachedEnemyLocations, World, Owner, HpPct);
        const float OptScore   = EvalOptimalScore(Loc, CachedEnemyLocations, World, Owner, HpPct);

        ELocationCategory BestCat;
        float BestScore;
        if (SafeScore >= AggrScore && SafeScore >= OptScore) { BestCat = ELocationCategory::Safe;       BestScore = SafeScore; }
        else if (AggrScore >= SafeScore && AggrScore >= OptScore) { BestCat = ELocationCategory::Aggressive; BestScore = AggrScore; }
        else { BestCat = ELocationCategory::Optimal; BestScore = OptScore; }

        FLocationCandidate Cand;
        Cand.Category       = BestCat;
        Cand.Location       = Loc;
        Cand.Score          = BestScore;
        Cand.DistanceToEnemy = CalcDistToNearestEnemy(Loc, CachedEnemyLocations);
        Cand.CoverRating    = CalcCoverRating(Loc, CachedEnemyLocations, World, Owner);
        Cand.HeightDelta    = CachedEnemyLocations.IsEmpty() ? 0.f
            : Loc.Z - CachedEnemyLocations[0].Z;
        AllCandidates.Add(Cand);
    }

    // ── 카테고리별 Top-3 추리기 ───────────────────────────────────────────────
    TMap<ELocationCategory, TArray<FLocationCandidate*>> ByCategory;
    for (FLocationCandidate& C : AllCandidates)
        ByCategory.FindOrAdd(C.Category).Add(&C);

    TArray<FLocationCandidate> Pruned;
    for (auto& KV : ByCategory)
    {
        KV.Value.Sort([](const FLocationCandidate& A, const FLocationCandidate& B){ return A.Score > B.Score; });
        const int32 TopN = FMath::Min(3, KV.Value.Num());
        int32 Idx = 0;
        for (int32 k = 0; k < TopN; ++k)
        {
            FLocationCandidate C = *KV.Value[k];
            C.CandidateId = FString::Printf(TEXT("%s_%d"), *CategoryToString(C.Category), Idx++);
            Pruned.Add(C);
        }
    }

    if (Pruned.IsEmpty())
    {
        TacticalQueryState = ETacticalQueryState::Failed;
        return;
    }

    // ── TacticalCandidateMap 저장 (LLM 응답 역조회용) ─────────────────────────
    TacticalCandidateMap.Empty();
    for (const FLocationCandidate& C : Pruned)
        TacticalCandidateMap.Add(C.CandidateId, C.Location);

    // ── JSON Payload 직렬화 ──────────────────────────────────────────────────
    const FString AgentID = GetOwnerAgentID();

    // context_summary 간략 문자열
    FString ContextSummary = FString::Printf(TEXT("HP:%.0f%% Enemies:%d"),
        HpPct * 100.f, CachedEnemyLocations.Num());
    if (StateComponent)
    {
        const FBehavioralTraits& Behavior = StateComponent->GetAttributes().Behavior;
        ContextSummary += FString::Printf(TEXT(" Aggr:%.0f Fear:%.0f"),
            Behavior.Aggression, Behavior.Fear);
    }

    TArray<TSharedPtr<FJsonValue>> CandidateArray;
    for (const FLocationCandidate& C : Pruned)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("id"),        C.CandidateId);
        Obj->SetStringField(TEXT("category"),  CategoryToString(C.Category));
        Obj->SetNumberField(TEXT("dist_to_enemy"), C.DistanceToEnemy);
        Obj->SetNumberField(TEXT("cover_rating"),  C.CoverRating);
        Obj->SetNumberField(TEXT("height_delta"),  C.HeightDelta);
        Obj->SetNumberField(TEXT("score"),         C.Score);
        CandidateArray.Add(MakeShared<FJsonValueObject>(Obj));
    }

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("agent_id"),        AgentID);
    Payload->SetStringField(TEXT("context_summary"), ContextSummary);
    Payload->SetArrayField(TEXT("candidates"),       CandidateArray);

    FString PayloadStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

    const FString Envelope = FEnvelopeBuilder::BuildLocationDecisionRequest(PayloadStr);

    // ── LLMClient로 전송 ─────────────────────────────────────────────────────
    if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->SendEnvelopePromptToLLM(Envelope);
            TacticalQueryState = ETacticalQueryState::WaitingLLM;
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - location_decision 전송 (후보 %d개)"),
                *AgentID, Pruned.Num());
                
            // LLM 전송 성공 후 시각화 & 구조화 로그 출력
            DrawEQSCandidates(Pruned, EQSDebugDuration);

            UE_LOG(LogTemp, Log, TEXT("=== [EQS Candidates] %s ==="), *AgentID);
            for (const FLocationCandidate& C : Pruned)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("  [%s] Cat=%s Score=%.2f Dist=%.0f Cover=%.2f Loc=(%.0f,%.0f,%.0f)"),
                    *C.CandidateId, *CategoryToString(C.Category),
                    C.Score, C.DistanceToEnemy, C.CoverRating,
                    C.Location.X, C.Location.Y, C.Location.Z);
            }
        }
        else
        {
            TacticalQueryState = ETacticalQueryState::Failed;
        }
    }
    else
    {
        TacticalQueryState = ETacticalQueryState::Failed;
    }
}

void UNPCActionComponent::NotifyLocationDecisionReady(const FString& ChosenCandidateId, const FString& Reason)
{
    const FVector* Found = TacticalCandidateMap.Find(ChosenCandidateId);
    if (!Found)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NPCAction] NotifyLocationDecisionReady - 알 수 없는 CandidateId: %s. Fallback 최초 항목 사용."),
            *ChosenCandidateId);

        // Fallback: 맵의 첫 항목 사용
        if (TacticalCandidateMap.Num() > 0)
        {
            TacticalQueryResult = TacticalCandidateMap.CreateConstIterator().Value();
        }
        else
        {
            TacticalQueryState = ETacticalQueryState::Failed;
            return;
        }
    }
    else
    {
        TacticalQueryResult = *Found;
    }

    // Move 액션을 큐에 주입 → ProcessNextAction()이 정상 처리
    FGameAction MoveAction;
    MoveAction.ActionType = EAction::Move;
    MoveAction.Parameters.Add(TEXT("TargetLoc"), TacticalQueryResult.ToString());
    ActionQueue.Enqueue(MoveAction);

    TacticalQueryState = ETacticalQueryState::ResultReady;
    
    DrawEQSChosenLocation(TacticalQueryResult, ChosenCandidateId, Reason, EQSDebugDuration);

    // 최종 선택 좌표 표준 로그 (구조 통일)
    UE_LOG(LogTemp, Log,
        TEXT("[EQS RESULT] AgentID=%s | ChosenID=%s | X=%.1f Y=%.1f Z=%.1f"),
        *GetOwnerAgentID(), *ChosenCandidateId,
        TacticalQueryResult.X, TacticalQueryResult.Y, TacticalQueryResult.Z);
}

void UNPCActionComponent::ExecuteFollow(AActor* TargetActor, EMoveType SpeedType)
{
    if (!TargetActor) return;
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;
    FVector Dir = (OwnerCharacter->GetActorLocation() - TargetActor->GetActorLocation()).GetSafeNormal();
    BaseMove(TargetActor->GetActorLocation() + Dir * 300.f, SpeedType);
}

void UNPCActionComponent::ExecuteDialogue(const FString& DialogueText, const EFacialState Emotion) { BaseDialogue(DialogueText, Emotion); }

void UNPCActionComponent::ExecuteTurnTo(FVector TargetLocation, AActor* TargetActor)
{
    BaseFaceRotate(TargetActor ? TargetActor->GetActorLocation() : TargetLocation);
}

void UNPCActionComponent::ExecuteScan(FVector TargetLocation, AActor* TargetActor)
{
    FVector Focus = TargetActor ? TargetActor->GetActorLocation() : TargetLocation;
    BaseFaceRotate(Focus + FVector(FMath::VRand().X, FMath::VRand().Y, 0.f) * 100.f, 3.f);
}

void UNPCActionComponent::ExecuteUseItem(const FString& ItemID)
{
    if (InventoryComponent && InventoryComponent->RemoveItem(ItemID, 1))
    {
        BasePlayActionMedia(TEXT("Eat"));
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 사용: %s"), *ItemID);
        if (ACharacter* C = Cast<ACharacter>(GetOwner()))
            UAISense_Hearing::ReportNoiseEvent(GetWorld(), C->GetActorLocation(), NPCActionKeys::Noise_UseItem, C, 0.f, NPCActionKeys::NoiseTag_UseItem);
    }
}

void UNPCActionComponent::ExecuteEquipAction(const FString& ItemID)
{
    if (InventoryComponent) { InventoryComponent->EquipItem(ItemID); UE_LOG(LogTemp, Log, TEXT("[NPCAction] 장착: %s"), *ItemID); }
}

void UNPCActionComponent::ExecuteUnequipAction(const FString& ItemID)
{
    if (InventoryComponent) { InventoryComponent->UnequipItemByID(ItemID); UE_LOG(LogTemp, Log, TEXT("[NPCAction] 해제: %s"), *ItemID); }
}

// ==========================================
// [2] Combat Behaviors
// ==========================================

void UNPCActionComponent::ExecuteAttackAction(AActor* TargetActor, EAttackType AttackType)
{
    if (!TargetActor) return;
    ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    BaseMove(TargetActor->GetActorLocation(), EMoveType::Run);
    BasePlayActionMedia(TEXT("Attack"));
    if (ACharacter* C = Cast<ACharacter>(GetOwner()))
        UAISense_Hearing::ReportNoiseEvent(GetWorld(), C->GetActorLocation(), NPCActionKeys::Noise_Attack, C, 0.f, NPCActionKeys::NoiseTag_Attack);
}

void UNPCActionComponent::ExecuteBlock(AActor* TargetActor)
{
    if (TargetActor) ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    BasePlayActionMedia(TEXT("Block"));
}

void UNPCActionComponent::ExecuteDodgeAction(FVector Direction)
{
    ExecuteTurnTo(GetOwner()->GetActorLocation() + Direction * 100.f, nullptr);
    BasePlayActionMedia(TEXT("Dodge"));
}

void UNPCActionComponent::ExecuteFlee(FVector EscapeLocation)   { BaseMove(EscapeLocation, EMoveType::Run); }
void UNPCActionComponent::ExecuteSignalAllies(const FString& HandSign) { BaseSignalAllies(HandSign); }

// ==========================================
// [3] Social Behaviors
// ==========================================

void UNPCActionComponent::ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, int32 GiveAmount, const FString& GetItemID, int32 GetAmount)
{
    // TODO: 레시피 검증 후 GiveItem 실행
    ExecuteGiveItem(TargetActor, GiveItemID, GiveAmount);
}

void UNPCActionComponent::ExecuteGiveItem(AActor* TargetActor, const FString& ItemID, int32 Amount)
{
    if (InventoryComponent && InventoryComponent->HasItem(ItemID, Amount))
    {
        InventoryComponent->RemoveItem(ItemID, Amount);
        ExecuteTurnTo(FVector::ZeroVector, TargetActor);
        BasePlayActionMedia(TEXT("Give"));
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 전달: %s"), *ItemID);
    }
    else { UE_LOG(LogTemp, Error, TEXT("[NPCAction] 아이템 없음: %s"), *ItemID); }
}

void UNPCActionComponent::ExecuteComfort(AActor* TargetActor) { BaseComfort(TargetActor); }

void UNPCActionComponent::ExecuteHandObject(const FString& ItemID)
{
    if (InventoryComponent && InventoryComponent->HasItem(ItemID))
    {
        InventoryComponent->EquipItem(ItemID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 제시: %s"), *ItemID);
    }
}

// ==========================================
// [4] Task Behaviors
// ==========================================

void UNPCActionComponent::ExecutePickUp(FVector Location)
{
    BaseMove(Location, EMoveType::Walk);
    BasePlayActionMedia(TEXT("PickUp"));

    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    UItemManager* ItemManager = GI ? GI->GetSubsystem<UItemManager>() : nullptr;
    if (!ItemManager || !InventoryComponent) return;

    for (const auto& Pair : BaseDetectEntityInRange(100.f, EEntityType::Item))
    {
        FItemData Data;
        if (ItemManager->GetItemDataByID(Pair.Key, Data))
        {
            InventoryComponent->AddItem(Data, Pair.Value);
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] 줍기: %s x%d"), *Pair.Key, Pair.Value);
        }
        else { UE_LOG(LogTemp, Error, TEXT("[NPCAction] 아이템 데이터 없음: %s"), *Pair.Key); }
    }
}

void UNPCActionComponent::ExecuteDrop(const FString& TargetTemplateID)
{
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter || !InventoryComponent) return;

    if (!InventoryComponent->RemoveItem(TargetTemplateID, 1))
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 드랍 실패 (인벤토리 없음): %s"), *TargetTemplateID);
        return;
    }

    BasePlayActionMedia(TEXT("Drop"));
    UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_Drop, OwnerCharacter, 0.f);

    UGameInstance* GI = OwnerCharacter->GetGameInstance();
    UItemManager* ItemManager = GI ? GI->GetSubsystem<UItemManager>() : nullptr;
    if (!IsValid(ItemManager)) return;

    // TODO: TargetTemplateID → BP 클래스 매핑 후 SpawnActor 구현
    AActor* SpawnedItem = nullptr;
    if (IsValid(SpawnedItem))
    {
        const FString UUID = FGuid::NewGuid().ToString();
        ItemManager->RegisterDroppedItem(UUID, SpawnedItem, TargetTemplateID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 드랍 등록: %s / %s"), *TargetTemplateID, *UUID);
    }
    else { UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 드랍 스폰 미구현(TODO): %s"), *TargetTemplateID); }
}

void UNPCActionComponent::ExecuteCraft(const TArray<FString>& ItemIDs)
{
    // TODO: 레시피 검증 및 소모/생성 연동
    BasePlayActionMedia(TEXT("Craft"));
}

void UNPCActionComponent::ExecuteRepair(const FString& ItemID)
{
    BasePlayActionMedia(TEXT("Repair"));
    if (InventoryComponent && InventoryComponent->HasItem(ItemID))
    {
        float Amount = StateComponent ? StateComponent->GetAttributes().BaseStats.Perception : 10.f;
        InventoryComponent->RepairItem(ItemID, Amount);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 수리: %s"), *ItemID);
    }
    else { UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 수리 실패 (미보유): %s"), *ItemID); }
}

// ==========================================
// [5] Investigation Behaviors
// ==========================================

void UNPCActionComponent::ExecuteInvestigate(FVector Location)
{
    BaseMove(Location, EMoveType::Walk);
    ExecuteScan(Location, nullptr);
}

void UNPCActionComponent::ExecuteScout(FVector StartLocation, FVector EndLocation)
{
    // TODO: BT Loop와 결합해 왕복 순찰로 발전
    FVector Dest = EndLocation;
    if (AActor* Owner = GetOwner())
    {
        const FVector Cur = Owner->GetActorLocation();
        Dest = (FVector::DistSquared(Cur, StartLocation) < FVector::DistSquared(Cur, EndLocation)) ? StartLocation : EndLocation;
    }
    BaseMove(Dest, EMoveType::Walk);
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

