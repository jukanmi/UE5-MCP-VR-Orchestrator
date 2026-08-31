#include "NPCActionComponent.h"
#include "../../Core/GameplayTagUtils.h"
#include "../NPCStateComponent.h"
#include "../NPCInventoryComponent.h"
#include "SmartNPCAIController.h"
#include "../Struct/NPCActionKeys.h"
#include "../NPCActionDataAsset.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "../../Inventory/InventoryComponent.h"
#include "../../Inventory/ItemManager.h"
#include "Perception/AISense_Hearing.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "../SmartNPC.h"
#include "../../Network/EnvelopeBuilder.h"
#include "../NPCManager.h"
#include "../../Utils/DiceSystem.h" // [추가] 패닉 주사위 판정용
#include "Kismet/GameplayStatics.h" // 액션 미디어 사운드 재생
#include "../../Furniture/FurnitureActor.h" // Sit/Sleep 가구 스냅·점유
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
            case EAction::Track:
                return FName("State.Action.Investigation.Track");

            // Lifestyle
            // 주의: Sit/Sleep/Read/Pray 는 .ini 에 태그가 등록돼 있는데도 여기 매핑이 없어
            // 런타임에 태그를 못 받는다(기존 갭, 2026-08-02 발견 — 별건으로 정리 필요).
            case EAction::StandUp:
                return FName("State.Action.Lifestyle.StandUp");

            default:
                return NAME_None;
        }
    }

    void ResetAllStateTagsToIdle(AActor* Target)
    {
        if (ASmartNPC* NPC = Cast<ASmartNPC>(Target))
        {
            // 컨테이너 직접 조작 금지 — 일괄 리셋도 공유 헬퍼 경유
            GameplayTagUtils::ResetAllStates(NPC->GameplayTags);
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

}

UNPCActionComponent::UNPCActionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    // === 척수반사 기본 룰 ===
    // 위에서부터 처음 맞는 하나만 발동한다 — 좁은 조건이 먼저.
    // 거리 상한은 감지 반경(SightRadius 3000 / HearingRange 3000) 안에서만 의미가 있다.
    {
        FReflexRule& R = ReflexRules.AddDefaulted_GetRef();
        R.RuleName = TEXT("적대·근접 즉시 공격");
        R.Sense = ESenseType::Sight;
        R.Relation = ENPCRelation::Hostile;
        R.MaxDistance = 500.f;
        R.Cooldown = 3.f;
        R.bEnterCombat = true;
        R.ActionWeights.Add(EAction::Attack, 100.f);
    }
    {
        FReflexRule& R = ReflexRules.AddDefaulted_GetRef();
        R.RuleName = TEXT("적대·원거리 공격 or 아군 호출");
        R.Sense = ESenseType::Sight;
        R.Relation = ENPCRelation::Hostile;
        R.Cooldown = 3.f;
        R.bEnterCombat = true;
        R.ActionWeights.Add(EAction::Attack, 80.f);
        R.ActionWeights.Add(EAction::SignalAllies, 20.f);
    }
    {
        FReflexRule& R = ReflexRules.AddDefaulted_GetRef();
        R.RuleName = TEXT("중립·고위험 경계");
        R.Sense = ESenseType::Sight;
        R.Relation = ENPCRelation::Neutral;
        R.MinBaseDanger = 0.5f;
        R.Cooldown = 10.f;
        R.ActionWeights.Add(EAction::Scan, 60.f);
        R.ActionWeights.Add(EAction::SignalAllies, 40.f);
    }
    {
        // 비전투 생동 반응 — Mode 전환 없음. 쿨다운이 perception tick(9초)보다 훨씬 길어야
        // 시야에 든 아군을 향해 계속 돌아보는 꼴이 안 난다.
        FReflexRule& R = ReflexRules.AddDefaulted_GetRef();
        R.RuleName = TEXT("친화·근거리 인사");
        R.Sense = ESenseType::Sight;
        R.Relation = ENPCRelation::Friendly;
        R.MaxDistance = 800.f;
        R.Cooldown = 30.f;
        R.ActionWeights.Add(EAction::TurnTo, 100.f);
    }
    {
        FReflexRule& R = ReflexRules.AddDefaulted_GetRef();
        R.RuleName = TEXT("물건 떨어지는 소리 조사");
        R.Sense = ESenseType::Hearing;
        R.EventTypeContains = TEXT("Drop");
        R.Cooldown = 8.f;
        R.ActionWeights.Add(EAction::Investigate, 60.f);
        R.ActionWeights.Add(EAction::TurnTo, 40.f);
    }
    {
        FReflexRule& R = ReflexRules.AddDefaulted_GetRef();
        R.RuleName = TEXT("아이템 사용 소리 쳐다보기");
        R.Sense = ESenseType::Hearing;
        R.EventTypeContains = TEXT("UseItem");
        R.Cooldown = 8.f;
        R.ActionWeights.Add(EAction::TurnTo, 70.f);
        R.ActionWeights.Add(EAction::Investigate, 30.f);
    }
}

bool UNPCActionComponent::DoesReflexRuleMatch(const FReflexRule& Rule, ESenseType Sense, const FString& EventType,
                                              ENPCRelation Relation, float BaseDanger, float Distance) const
{
    if (Rule.Sense != Sense) return false;

    if (Rule.Relation != ENPCRelation::Any && Rule.Relation != Relation) return false;

    if (!Rule.EventTypeContains.IsEmpty() && !EventType.Contains(Rule.EventTypeContains)) return false;

    if (Rule.MaxDistance > 0.f && Distance > Rule.MaxDistance) return false;

    if (BaseDanger < Rule.MinBaseDanger) return false;

    // 액션이 하나도 없는 룰은 발동해봐야 아무 일도 안 일어난다 — 매칭 단계에서 배제.
    return Rule.ActionWeights.Num() > 0;
}

EAction UNPCActionComponent::PickWeightedReflexAction(const TMap<EAction, float>& Weights)
{
    float TotalW = 0.f;
    for (const TPair<EAction, float>& Pair : Weights) TotalW += FMath::Max(0.f, Pair.Value);
    if (TotalW <= KINDA_SMALL_NUMBER) return EAction::Idle;

    float Roll = FMath::FRandRange(0.f, TotalW);
    EAction Last = EAction::Idle;
    for (const TPair<EAction, float>& Pair : Weights)
    {
        const float W = FMath::Max(0.f, Pair.Value);
        if (W <= 0.f) continue;
        Last = Pair.Key;                 // 부동소수 잔여로 못 고를 때의 폴백
        if (Roll < W) return Pair.Key;
        Roll -= W;
    }
    return Last;
}

bool UNPCActionComponent::TryReflexReact(ESenseType Sense, const FString& EventType, const FString& SourceID,
                                         float BaseDanger, float Distance, const FVector& StimulusLoc)
{
    UWorld* World = GetWorld();
    if (!World || !StateComponent || ReflexRules.Num() == 0) return false;

    // 진행 중 액션·대기 큐가 있으면 개입하지 않는다(셀렉터와 동일 규율).
    if (bIsBusy || !ActionQueue.IsEmpty()) return false;

    const float Now = World->GetTimeSeconds();
    if (Now - LastReflexTime < ReflexGlobalCooldown) return false;

    // 룰별 쿨다운 배열을 테이블 크기에 맞춘다(에디터에서 룰을 늘렸을 수 있음).
    if (ReflexRuleLastFireTime.Num() != ReflexRules.Num())
    {
        ReflexRuleLastFireTime.Init(-1000.f, ReflexRules.Num());
    }

    const ENPCRelation Relation = StateComponent->GetRelation(SourceID);

    for (int32 i = 0; i < ReflexRules.Num(); ++i)
    {
        const FReflexRule& Rule = ReflexRules[i];
        if (!DoesReflexRuleMatch(Rule, Sense, EventType, Relation, BaseDanger, Distance)) continue;

        // 매칭은 됐지만 쿨다운 중 — 아래 룰로 흘리지 않고 여기서 끝낸다.
        // (넘기면 더 약한 룰이 대신 튀어 같은 자극에 계속 반응하는 꼴이 된다)
        if (Now - ReflexRuleLastFireTime[i] < Rule.Cooldown) return false;

        const EAction Chosen = PickWeightedReflexAction(Rule.ActionWeights);
        if (Chosen == EAction::Idle) return false;

        FGameAction Action;
        Action.ActionType = Chosen;
        switch (Chosen)
        {
        case EAction::Attack:
            Action.FacialState = EFacialState::Angry;
            Action.Parameters.Add(NPCActionKeys::Key_TargetID, SourceID);
            break;

        case EAction::SignalAllies:
            // target_id 가 미디어 키 겸용(ExecuteSignalAllies → BasePlayActionMedia).
            Action.Parameters.Add(NPCActionKeys::Key_TargetID, TEXT("SignalAllies"));
            break;

        case EAction::Scan:
            Action.FacialState = EFacialState::Surprised;
            Action.Parameters.Add(NPCActionKeys::Key_TargetLoc, StimulusLoc.ToString());
            break;

        case EAction::TurnTo:
        case EAction::Investigate:
            // 위치 기반 — TargetActor 해석 없이도 동작하도록 좌표를 직접 싣는다.
            Action.Parameters.Add(NPCActionKeys::Key_TargetLoc, StimulusLoc.ToString());
            break;

        default:
            break;
        }

        ActionQueue.Enqueue(Action);
        LastQueuedActionType = Chosen;

        ReflexRuleLastFireTime[i] = Now;
        LastReflexTime = Now;

        // Combat 진입 — 반사가 SLM 을 대체하면서 Mode 를 올릴 주체도 여기로 옮겨왔다.
        if (Rule.bEnterCombat && StateComponent->GetBehaviorMode() != ENPCBehaviorMode::Combat)
        {
            StateComponent->SetBehaviorMode(ENPCBehaviorMode::Combat);
        }

        // LLM 이 "이미 반응했음"을 알아야 다음 replan 이 한 박자 늦은 지시를 안 만든다.
        StateComponent->NoteReflexAction(Chosen);

        UE_LOG(LogTemp, Log, TEXT("[Reflex] %s: rule='%s' src=%s dist=%.0f base=%.2f -> %s%s"),
            *GetOwnerAgentID(), *Rule.RuleName, *SourceID, Distance, BaseDanger,
            *UEnum::GetValueAsString(Chosen), Rule.bEnterCombat ? TEXT(" (Combat 진입)") : TEXT(""));

        return true;
    }

    return false;
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

        // AgentID 캐시 — SmartNPC 직접 의존을 피하기 위해 reflection으로 1회 조회.
        if (ASmartNPC* NPC = Cast<ASmartNPC>(Owner))
        {
            CachedAgentID = NPC->AgentID;
        }
        else if (FProperty* Prop = Owner->GetClass()->FindPropertyByName(TEXT("AgentID")))
        {
            if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
            {
                CachedAgentID = StrProp->GetPropertyValue_InContainer(Owner);
            }
        }
        if (CachedAgentID.IsEmpty())
        {
            CachedAgentID = Owner->GetName();
        }
    }
    else
    {
        CachedAgentID = TEXT("Unknown");
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

// BehaviorMode 소유는 NPCStateComponent. read 위임.
ENPCBehaviorMode UNPCActionComponent::GetBehaviorMode() const
{
    return StateComponent ? StateComponent->GetBehaviorMode() : ENPCBehaviorMode::Common;
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

// LLM 이 Parameters["style"] 로 보내는 이동 스타일 문자열을 EMoveType 으로 해석한다.
// 어휘 단일 소스는 EMoveType — Python 스키마(actions.py style 필드)가 이 이름을 그대로 쓴다.
EMoveType UNPCActionComponent::ParseMoveStyle(const FString& StyleStr) const
{
    if (StyleStr.IsEmpty()) return EMoveType::Walk;

    if (StyleStr.Equals(TEXT("Run"), ESearchCase::IgnoreCase))    return EMoveType::Run;
    if (StyleStr.Equals(TEXT("Sprint"), ESearchCase::IgnoreCase)) return EMoveType::Sprint;
    if (StyleStr.Equals(TEXT("Crouch"), ESearchCase::IgnoreCase)) return EMoveType::Crouch;
    if (StyleStr.Equals(TEXT("Walk"), ESearchCase::IgnoreCase))   return EMoveType::Walk;

    // 미지원 어휘는 조용히 삼키지 않는다 — Python 스키마 드리프트를 로그로 드러낸다.
    UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 미지원 style '%s' → Walk 폴백"), *StyleStr);
    return EMoveType::Walk;
}

// === Action Batch System ===


void UNPCActionComponent::ExecuteActionBatch(const FActionBatch& Batch)
{
    // 한 줄 요약 (Mode + 액션 타입 리스트). 상세는 Verbose.
    FString ActionList;
    for (int32 i = 0; i < Batch.Actions.Num(); ++i)
    {
        if (i > 0) ActionList += TEXT(",");
        ActionList += UEnum::GetValueAsString(Batch.Actions[i].ActionType);
    }
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s recv Mode=%s Actions=[%s]"),
        *GetOwnerAgentID(), *UEnum::GetValueAsString(Batch.Mode), *ActionList);

    for (int32 i = 0; i < Batch.Actions.Num(); ++i)
    {
        const FGameAction& Action = Batch.Actions[i];
        FString ParamStr;
        for (auto& Pair : Action.Parameters)
        {
            ParamStr += FString::Printf(TEXT("%s=%s, "), *Pair.Key, *Pair.Value);
        }
        UE_LOG(LogTemp, Verbose, TEXT("  [%d] %s (Facial: %s) %s"),
            i, *UEnum::GetValueAsString(Action.ActionType),
            *UEnum::GetValueAsString(Action.FacialState), *ParamStr);
    }

    // BehaviorMode(Common/Combat) 갱신 — StateComponent가 단일 소유. STTask Combat 분기가 이 값을 읽는다.
    //
    // 빈 배치는 Mode 를 건드리지 않는다. emergency_report 가 통보 전용이 된 뒤로 Python 은
    // 행동 없는 응답(Mode=Common)을 정상적으로 돌려주는데, 그걸 그대로 반영하면 척수반사가
    // 방금 올린 Combat 이 곧바로 되돌아간다. combat_victory 의 빈 배치도 같은 이유로 안전해진다.
    // "행동을 지시하지 않은 응답"에 모드 전환 권한을 주지 않는 것이 요점.
    if (Batch.Actions.Num() > 0)
    {
        if (StateComponent) StateComponent->SetBehaviorMode(Batch.Mode);

        // LLM 이 전투 밖으로 전환시키면 셀렉터 연속성도 새 전투 기준으로 초기화.
        if (Batch.Mode != ENPCBehaviorMode::Combat)
        {
            ResetCombatSelectorState();
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
            // 동일 액션 타입이 큐 끝에 이미 있으면 추가하지 않음 — 이벤트 폭증 시 같은 액션 반복 큐잉 방지
            if (Action.ActionType == LastQueuedActionType)
            {
                UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] %s - 중복 액션 스킵: %s"),
                    *GetOwnerAgentID(), *UEnum::GetValueAsString(Action.ActionType));
                continue;
            }
            // [의도(Why)] 일반 물리적 액션은 이전 행동이 끝나길 기다렸다가 순차적으로 실행(Queue)되도록 보장합니다.
            ActionQueue.Enqueue(Action);
            LastQueuedActionType = Action.ActionType;
            UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] %s - Action Queued. Action: %s"),
                *GetOwnerAgentID(), *UEnum::GetValueAsString(Action.ActionType));
        }
    }

}

// === Action Queue System ===

void UNPCActionComponent::ClearActiveActionState()
{
    bIsBusy = false;
    bActionAwaitingAsync = false;
    PendingMoveMediaKey.Reset();
    PendingFurnitureTarget.Reset(); // 이동 중단 시 스테일 가구 목적지 방지 — 점유 전이라 Release 불필요
    StopDodgeMove(); // Dodge 마찰·제동 원복 — 정상 종료·중단·워치독 공통 경로
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ActionWatchdogTimer);
        World->GetTimerManager().ClearTimer(FleePanicTimer); // 패닉 지연 Flee 도 함께 취소
    }
}

void UNPCActionComponent::ReleaseOccupiedFurniture()
{
    // 가구가 먼저 파괴됐으면 약참조 invalid → 조용히 스킵.
    if (AFurnitureActor* Furniture = OccupiedFurniture.Get())
    {
        Furniture->Release(GetOwner());
    }
    OccupiedFurniture.Reset();
}

void UNPCActionComponent::ResetPostureFlags()
{
    // Sit/Sleep 점유 해제 — 자세와 동일 라이프사이클(지속 상태).
    ReleaseOccupiedFurniture();

    if (!StateComponent) return;
    StateComponent->bIsSit = false;
    StateComponent->bIsLie = false;
}

void UNPCActionComponent::StopAllActions()
{
    ActionQueue.Empty();
    ClearActiveActionState();
    ResetPostureFlags(); // 사망·넉다운·전투종료 등 전면 정지 — 앉/눕 자세도 해제(AnimBP 자세 고착 방지)
    LastQueuedActionType = EAction::Idle;
    ResetCombatSelectorState(); // 전투 종료(HandleCombatTargetDead)·비상 정지 공통 — 셀렉터 연속성 초기화

    ResetAllStateTagsToIdle(GetOwner());

    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        AI->StopMovement();
    }

    // Track 타이머 해제 (워치독은 ClearActiveActionState가 처리)
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TrackTimer);
    }
    TrackedTarget.Reset();

    OnActionStoppedAll.Broadcast();

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Stopped All Actions."), *GetOwnerAgentID());
}

bool UNPCActionComponent::ProcessNextAction()
{
    if (bIsBusy || ActionQueue.IsEmpty()) return false;

    if (ActionQueue.Dequeue(CurrentAction))
    {
        bIsBusy = true;

        // Track 이외 명령이 오면 추적 즉시 해제 — 새 명령이 추적을 덮어쓰는 게 자연스러운 동작
        if (CurrentAction.ActionType != EAction::Track && TrackedTarget.IsValid())
        {
            if (UWorld* World = GetWorld())
                World->GetTimerManager().ClearTimer(TrackTimer);
            TrackedTarget.Reset();
        }

        // 현재 액션 단일 소스 = CurrentAction. GameplayTags(State.Action.*)는 파생 미러.
        TransitionStateTag(GetOwner(), CurrentAction.ActionType);

        // 물리적 액션 시작 전 상태(Facial) 업데이트
        UpdateActionState(CurrentAction);

        OnActionStarted.Broadcast(CurrentAction);

        UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Starting Action '%s'"), *GetOwnerAgentID(), *UEnum::GetValueAsString(CurrentAction.ActionType));
        return true;
    }
    return false;
}

void UNPCActionComponent::OnActionCompleted()
{
    // 이미 완료 처리됨 — 비동기 콜백과 워치독이 경합해도 한 번만 완료시킨다.
    if (!bIsBusy) return;

    ClearActiveActionState();

    // 완료된 액션 = CurrentAction (단일 소스). 태그 revert에 사용.
    const EAction CompletedAction = CurrentAction.ActionType;

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Action '%s' Completed."),
        *GetOwnerAgentID(), *UEnum::GetValueAsString(CompletedAction));

    RevertStateTagToIdle(GetOwner(), CompletedAction);
}

void UNPCActionComponent::AbortCurrentAction()
{

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: ABORTING Action"), *GetOwnerAgentID());

    // 비동기 대기/워치독 해제 — Abort 후 콜백이 늦게 와도 OnActionCompleted 가드가 막는다.
    ClearActiveActionState();

    // 아래 StopAnimMontage 가 앉/눕 포즈를 떨구므로 자세 플래그도 함께 해제.
    ResetPostureFlags();

    // 중단된 액션 = CurrentAction (단일 소스). 태그 revert에 사용.
    RevertStateTagToIdle(GetOwner(), CurrentAction.ActionType);

    // Track 타이머 해제
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TrackTimer);
    }
    TrackedTarget.Reset();

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
        // 도착(또는 실패/취소) 시 OnMoveActionCompleted가 액션 완료를 처리하도록 바인딩.
        // 이동 액션은 비동기 — bIsBusy를 도착까지 유지해 큐가 다음 액션으로 넘어가지 않게 한다.
        if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
        {
            PFC->OnRequestFinished.RemoveAll(this);
            PFC->OnRequestFinished.AddUObject(this, &UNPCActionComponent::OnMoveActionCompleted);
        }
        bActionAwaitingAsync = true;
        const EPathFollowingRequestResult::Type MoveResult = AIController->MoveToLocation(TargetLocation, AcceptanceRadius);
        HandleImmediateMoveResult(AIController, MoveResult); // AlreadyAtGoal/Failed 는 콜백 미발화 — 동기 처리
    }
}

void UNPCActionComponent::HandleImmediateMoveResult(AAIController* AIController, EPathFollowingRequestResult::Type MoveResult)
{
    if (MoveResult == EPathFollowingRequestResult::RequestSuccessful) return;

    // AlreadyAtGoal/Failed 는 OnRequestFinished 가 발화하지 않음 — 대기 유지 시
    // 워치독(MaxActionDuration)까지 정지. 즉시 결과는 여기서 동기 처리한다.
    if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
    {
        PFC->OnRequestFinished.RemoveAll(this); // stale 바인딩이 무관한 후속 이동에 발화하는 것 방지
    }
    bActionAwaitingAsync = false;

    const FString MediaKey = PendingMoveMediaKey;
    PendingMoveMediaKey.Reset();
    if (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal && !MediaKey.IsEmpty())
    {
        // 이미 목적지 — 대기 몽타주(Attack/SitDown 등) 즉시 재생. 재생 성공 시 비동기 완료로 전환,
        // 실패 시 bActionAwaitingAsync=false 라 ExecuteInteraction 말미가 즉시 완료 처리.
        PlayActionMediaWithPosture(MediaKey);
    }
}

bool UNPCActionComponent::PlayActionMediaWithPosture(const FString& MediaKey)
{
    // 가구 목적지가 있는 Lifestyle(Sit/Sleep) — 점유 시도 성공 시 SeatPoint 로 스냅 후 몽타주.
    // 실패(타인 점유)면 스냅 생략, 제자리 재생 폴백. 두 도착 경로(OnMoveActionCompleted·
    // HandleImmediateMoveResult)가 모두 여길 통과하므로 스냅·점유의 유일한 삽입 지점.
    // (PendingFurnitureTarget 은 가구行 경로만 세팅하므로 미디어 키 조건 불필요.)
    if (PendingFurnitureTarget.IsValid())
    {
        AFurnitureActor* Furniture = PendingFurnitureTarget.Get();
        if (AActor* Owner = GetOwner())
        {
            if (Furniture->TryOccupy(Owner))
            {
                const FTransform SeatXf = Furniture->GetSeatTransform();
                Owner->SetActorLocationAndRotation(SeatXf.GetLocation(), SeatXf.GetRotation(),
                    false, nullptr, ETeleportType::TeleportPhysics);
                OccupiedFurniture = Furniture;
            }
        }
        PendingFurnitureTarget.Reset();
    }

    const bool bPlayed = BasePlayActionMedia(MediaKey);

    // 자세 플래그는 몽타주가 실제 재생될 때만 — 미디어 미등록인데 상태만 '앉음'이 되는 불일치 방지.
    if (bPlayed && StateComponent)
    {
        if (MediaKey == NPCActionKeys::Interact_SitDown)      StateComponent->bIsSit = true;
        else if (MediaKey == NPCActionKeys::Interact_LieDown) StateComponent->bIsLie = true;
    }
    return bPlayed;
}

void UNPCActionComponent::BaseMoveToActor(AActor* TargetActor, EMoveType SpeedType, float AcceptanceRadius)
{
    if (!TargetActor) return;
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;

    if (UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement())
    {
        MovementComp->MaxWalkSpeed = ParseMoveSpeed(SpeedType);
    }

    if (AAIController* AIController = Cast<AAIController>(OwnerCharacter->GetController()))
    {
        // 스냅샷 좌표 MoveToLocation 과 달리 MoveToActor 는 이동 중 타겟을 추적(자동 재경로).
        // AcceptanceRadius 이내 도달 시 OnMoveActionCompleted — 완료·몽타주 체인은 BaseMove 동일.
        if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
        {
            PFC->OnRequestFinished.RemoveAll(this);
            PFC->OnRequestFinished.AddUObject(this, &UNPCActionComponent::OnMoveActionCompleted);
        }
        bActionAwaitingAsync = true;
        const EPathFollowingRequestResult::Type MoveResult = AIController->MoveToActor(TargetActor, AcceptanceRadius);
        HandleImmediateMoveResult(AIController, MoveResult); // AlreadyAtGoal/Failed 는 콜백 미발화 — 동기 처리
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
    {
        if (UWorld* World = GetWorld())
        {
            UAISense_Hearing::ReportNoiseEvent(World, OwnerCharacter->GetActorLocation(),
                NPCActionKeys::Noise_Dialogue, OwnerCharacter, 0.0f, NPCActionKeys::NoiseTag_Dialogue);
        }
    }
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
    // 제자리 착석 — 자세 플래그는 헬퍼가 몽타주 재생 성공 시에만 세운다(이동 경로와 동일 규칙).
    PlayActionMediaWithPosture(NPCActionKeys::Interact_SitDown);
}

void UNPCActionComponent::BaseSitUp()
{
    if (StateComponent) StateComponent->bIsSit = false;
    BasePlayActionMedia(NPCActionKeys::Interact_SitUp);
}

void UNPCActionComponent::BaseLieDown(AActor* TargetBed)
{
    PlayActionMediaWithPosture(NPCActionKeys::Interact_LieDown);
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
        // ItemManager 등록부 기반 조회 — 충돌 채널(ECC_PhysicsBody) 가정 없이 등록된 아이템만 정확히 탐지
        UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
        if (UItemManager* ItemMgr = GI ? GI->GetSubsystem<UItemManager>() : nullptr)
        {
            for (const FDroppedItemData& Item : ItemMgr->GetItemsInRange(OwnerCharacter->GetActorLocation(), SearchRadius))
            {
                DetectedEntities.FindOrAdd(Item.ItemTemplateID, 0)++;
            }
        }
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


bool UNPCActionComponent::BasePlayActionMedia(const FString& AssetID)
{
    if (!ActionData)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] ActionData Asset 설정 누락!"));
        return false;
    }

    bool bPlayedMontage = false;

    if (FActionMediaData* MediaData = ActionData->ActionMedias.Find(AssetID))
    {
        if (MediaData->Montage)
        {
            if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
            {
                const float Length = OwnerCharacter->PlayAnimMontage(MediaData->Montage);
                if (Length > 0.f)
                {
                    // 몽타주 종료 시 OnMontageActionEnded → OnActionCompleted. 재생 동안 bIsBusy 유지.
                    if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
                    {
                        if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                        {
                            FOnMontageEnded EndDel;
                            EndDel.BindUObject(this, &UNPCActionComponent::OnMontageActionEnded);
                            AnimInst->Montage_SetEndDelegate(EndDel, MediaData->Montage);
                            bActionAwaitingAsync = true;
                            bPlayedMontage = true;
                        }
                    }
                    UE_LOG(LogTemp, Log, TEXT("[NPCAction] DataAsset 몽타주 재생: %s"), *MediaData->Montage->GetName());
                }
            }
        }
        if (MediaData->Sound)
        {
            // 몽타주와 독립 재생(완료 모델 무관) — 3D 감쇠·볼륨은 SoundBase 에셋 설정을 따른다.
            UGameplayStatics::PlaySoundAtLocation(GetOwner(), MediaData->Sound, GetOwner()->GetActorLocation());
            UE_LOG(LogTemp, Log, TEXT("[NPCAction] DataAsset 사운드 재생: %s"), *MediaData->Sound->GetName());
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 지정한 마스터 에셋 미디어를 찾을 수 없음: %s"), *AssetID);
    }

    return bPlayedMontage;
}

// ----------------------------------------------------------------------------
// [EAction 래퍼 함수 (Action Wrappers)]

FVector UNPCActionComponent::ParseVectorParam(const FString& ParamStr) const
{
    FVector Result = FVector::ZeroVector;
    // FVector::ToString() → "X=1.0 Y=2.0 Z=3.0", Python → "(X=1,Y=2,Z=3)" 두 형식 모두 허용.
    // InitFromString은 FParse::Value로 X/Y/Z 키를 찾으므로 괄호 유무 무관하게 동작.
    if (!ParamStr.IsEmpty())
        Result.InitFromString(ParamStr);
    return Result;
}

void UNPCActionComponent::ExecuteInteraction(EAction ActionType, AActor* TargetActor, const TMap<FString, FString>& Params)
{
    // [비동기 완료 모델] 기본은 즉시형. BaseMove/BasePlayActionMedia가 콜백을 걸면 true가 되어
    // switch 종료 후 즉시 완료를 건너뛴다(콜백이 나중에 OnActionCompleted 호출).
    bActionAwaitingAsync = false;

    // 완료 신호가 끝내 오지 않는 경우(도달 불가 MoveTo, 몽타주 누락 등) 대비 워치독 시작.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(ActionWatchdogTimer, this,
            &UNPCActionComponent::HandleActionWatchdog, MaxActionDuration, false);
    }

    // [의도(Why)] 다양한 형태(위치, 텍스트, 다중 파라미터)의 JSON 인자를 BTTask 대신 컴포넌트 레벨에서 일괄 파싱 및 캐싱하여 각 세부 Execute 함수로 안전하게 전달합니다.
    // Parameters 딕셔너리 키는 Python 서버 snake_case 기준으로 단일화됨.
    FString TargetID = Params.FindRef(NPCActionKeys::Key_TargetID);
    // 아이템 ID — use/equip/give/drop/craft/repair 류. 비면 target_id 폴백(레거시 호환).
    FString ItemID = Params.FindRef(NPCActionKeys::Key_Item);
    if (ItemID.IsEmpty()) ItemID = TargetID;

    // style — Move 계열은 EMoveType 으로, Sing/Emote 는 미디어 키로 해석한다(액션별 의미가 다름).
    // ParseMoveStyle 은 Move 케이스에서만 호출 — Emote 의 style("Wave" 등)에 폴백 경고가 뜨지 않게.
    FString StyleStr = Params.FindRef(NPCActionKeys::Key_Style);
    // Sing/Emote 미디어 키. 비면 target_id 폴백(레거시 호환 — 구 송신측이 target_id 에 실어 보냄).
    FString LifestyleParam = StyleStr.IsEmpty() ? TargetID : StyleStr;

    // 위치: Python이 직접 보내는 경우는 없고, C++ 내부 주입(전술 쿼리 결과)만 존재 → Key_TargetLoc 단일 조회
    FVector Location = ParseVectorParam(Params.FindRef(NPCActionKeys::Key_TargetLoc));
    FVector Direction = ParseVectorParam(Params.FindRef(NPCActionKeys::Key_Direction));
    FString TextBody = Params.FindRef(NPCActionKeys::Key_Text);

    // Task, Social, Investigate 특수 파라미터 추출
    FVector StartLocation = ParseVectorParam(Params.FindRef(NPCActionKeys::Key_StartLocation));
    FVector EndLocation = ParseVectorParam(Params.FindRef(NPCActionKeys::Key_EndLocation));

    FString GiveItemID = Params.FindRef(NPCActionKeys::Key_GiveItemID);
    int32 GiveAmount = FMath::Max(1, FCString::Atoi(*Params.FindRef(NPCActionKeys::Key_GiveAmount)));
    FString GetItemID = Params.FindRef(NPCActionKeys::Key_GetItemID);
    int32 GetAmount = FMath::Max(1, FCString::Atoi(*Params.FindRef(NPCActionKeys::Key_GetAmount)));

    int32 Amount = FMath::Max(1, FCString::Atoi(*Params.FindRef(NPCActionKeys::Key_Amount)));

    TArray<FString> CraftItemIDs;
    FString CraftItemsStr = Params.FindRef(NPCActionKeys::Key_ItemIDs);
    if (CraftItemsStr.IsEmpty()) CraftItemsStr = ItemID;
    CraftItemsStr.ParseIntoArray(CraftItemIDs, TEXT(","), true);

    // 단일화된 EAction enum 값에 따라 세부적인 행동 함수로 라우팅합니다.
    switch (ActionType)
    {
    case EAction::Idle:         ExecuteIdle(); break;
    case EAction::Move:         ExecuteMove(Location, TargetActor, ParseMoveStyle(StyleStr)); break;
    case EAction::Follow:       ExecuteFollow(TargetActor, ParseMoveStyle(StyleStr)); break;
    case EAction::Dialogue:     ExecuteDialogue(TextBody, EFacialState::Neutral); break;
    case EAction::TurnTo:       ExecuteTurnTo(Location, TargetActor); break;
    case EAction::Scan:         ExecuteScan(Location, TargetActor); break;
    case EAction::UseItem:      ExecuteUseItem(ItemID); break;
    case EAction::Equip:        ExecuteEquipAction(ItemID); break;
    case EAction::Unequip:      ExecuteUnequipAction(ItemID); break;
    
    // Combat
    case EAction::Attack:       ExecuteAttackAction(TargetActor, EAttackType::Melee); break;
    case EAction::Block:        ExecuteBlock(TargetActor); break;
    case EAction::Dodge:        ExecuteDodgeAction(Direction.IsNearlyZero() ? FVector(100, 100, 0) : Direction); break;
    case EAction::Flee:
    {
        TWeakObjectPtr<UNPCActionComponent> WeakThis(this);
        // TargetActor 도 약참조 — 1.5초 패닉 지연 중 대상 파괴 시 use-after-free 방지
        TWeakObjectPtr<AActor> WeakTarget(TargetActor);
        auto DoFlee = [this, WeakThis, Location, WeakTarget]() {
            if (!WeakThis.IsValid()) return;   // 지연 타이머 발화 시 컴포넌트 GC 가드(use-after-free)
            FVector FleeTarget = Location;
            if (FleeTarget.IsNearlyZero())
            {
                // 위치 파라미터 없음 → EQS 전술 쿼리로 후퇴 위치 결정 시도.
                TArray<FVector> EnemyLocs;
                if (AActor* Target = WeakTarget.Get())
                {
                    EnemyLocs.Add(Target->GetActorLocation());
                }
                else if (ASmartNPCAIController* AICon = GetOwnerAIController())
                {
                    if (UBlackboardComponent* BB = AICon->GetBlackboardComponent())
                    {
                        const FVector LastKnown = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
                        if (!LastKnown.IsNearlyZero()) EnemyLocs.Add(LastKnown);
                    }
                }

                if (!EnemyLocs.IsEmpty() && TacticalQueryState == ETacticalQueryState::Idle)
                {
                    UE_LOG(LogTemp, Log, TEXT("[NPCAction] Flee → EQS 전술 쿼리로 후퇴 위치 결정 위임"));
                    TryStartTacticalQueryForCombat(EnemyLocs);
                    BaseEmotion(EFacialState::Fear);
                    return;
                }

                AActor* Owner = GetOwner();
                const FVector EnemyPos = !EnemyLocs.IsEmpty() ? EnemyLocs[0]
                                       : (Owner ? Owner->GetActorLocation() + Owner->GetActorForwardVector() * 100.f : FVector::ZeroVector);
                if (Owner && !EnemyPos.IsNearlyZero())
                {
                    const FVector AwayDir = (Owner->GetActorLocation() - EnemyPos).GetSafeNormal2D();
                    FleeTarget = Owner->GetActorLocation() + AwayDir * 1500.f;
                }
            }
            ExecuteFlee(FleeTarget);
        };

        // [인간화 2단계: 반사신경 실패 시 얼어붙기]
        FDiceResult ReflexResult;
        float Agility = StateComponent ? StateComponent->GetAttributes().BaseStats.Agility : 50.f;
        if (!UDiceSystem::CheckReflex(Agility, 60, ReflexResult)) // 난이도 60
        {
            UE_LOG(LogTemp, Warning, TEXT("[NPCAction] Panic! 반사신경(%.1f) 부족으로 %.1f초간 얼어붙음!"), Agility, 1.5f);
            BaseEmotion(EFacialState::Fear);
            // 1.5초 후 DoFlee 실행 — 멤버 핸들 사용: ClearActiveActionState 가 중단 시 취소 가능
            GetWorld()->GetTimerManager().SetTimer(FleePanicTimer, FTimerDelegate::CreateLambda(DoFlee), 1.5f, false);
        }
        else
        {
            DoFlee();
        }
        break;
    }
    case EAction::SignalAllies: ExecuteSignalAllies(TargetID); break;

    // Social
    case EAction::Trade:        ExecuteTrade(TargetActor, GiveItemID.IsEmpty() ? ItemID : GiveItemID, GiveAmount, GetItemID, GetAmount); break;
    case EAction::GiveItem:     ExecuteGiveItem(TargetActor, ItemID, Amount); break;
    case EAction::Comfort:      ExecuteComfort(TargetActor); break;
    case EAction::HandObject:   ExecuteHandObject(ItemID); break;

    // Task
    case EAction::PickUp:       ExecutePickUp(Location); break;
    case EAction::Drop:         ExecuteDrop(ItemID); break;
    case EAction::Craft:        ExecuteCraft(CraftItemIDs); break;
    case EAction::Repair:       ExecuteRepair(ItemID); break;
    
    // Investigate
    case EAction::Investigate:  ExecuteInvestigate(Location); break;
    case EAction::Track:        ExecuteTrack(TargetActor); break;
    case EAction::Scout:        ExecuteScout(StartLocation.IsNearlyZero() ? Location : StartLocation, EndLocation.IsNearlyZero() ? Location : EndLocation); break;
    
    // Lifestyle & Social (Emote)
    case EAction::Sit:
    case EAction::Sleep:
    case EAction::Read:
    case EAction::Pray:
    case EAction::Dance:
    case EAction::Sing:
    case EAction::Emote:
        ExecuteLifestyleAction(ActionType, TargetActor, Location, LifestyleParam);
        break;

    // 자세 해제는 가구·좌표가 필요 없어 ExecuteLifestyleAction 경로를 타지 않는다
    // (그쪽은 가구 타겟 필수 방어가 걸려 있어 무타겟이면 즉시 반환).
    case EAction::StandUp:      ExecuteStandUp(); break;

    case EAction::Wait:
        // 즉시형으로 처리(아래 tail에서 OnActionCompleted). duration 기반 실제 대기가 필요하면
        // 여기서 타이머를 걸고 bActionAwaitingAsync=true 후 만료 콜백에서 OnActionCompleted 호출하면 된다.
        break;

    default:
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 지원되지 않는 ActionType이 ExecuteInteraction으로 유입됨: %s"),
            *UEnum::GetValueAsString(ActionType));
        break;
    }

    // 비동기 콜백을 걸지 않은 즉시형 액션(Dialogue/Emotion/Equip/Wait 등)은 여기서 바로 완료.
    // BaseMove/BasePlayActionMedia를 탄 액션은 bActionAwaitingAsync=true → 콜백이 완료를 처리한다.
    if (!bActionAwaitingAsync)
    {
        OnActionCompleted();
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

UNPCActionComponent::FEQSWeights UNPCActionComponent::ComputeEQSWeights() const
{
    FEQSWeights W;
    if (!StateComponent) return W;

    const FNPCAttributes& Attr = StateComponent->GetAttributes();

    W.SearchRadius     = FMath::Clamp(EQS_SearchRadiusBase + Attr.BaseStats.Perception * EQS_PerceptionRadiusScale, 500.f, 3000.f);
    W.CoverWeight      = Attr.Behavior.Fear * 0.05f;
    W.DistanceWeight   = Attr.Behavior.Fear * 0.1f;   // 겁쟁이일수록 멀리 도망감

    if (Attr.HasStatusEffect(EStatusEffect::Feared))
    {
        W.CoverWeight    = 5.0f;
        W.DistanceWeight = 10.0f;
    }

    W.AggressionWeight = Attr.Behavior.Aggression * 0.1f;

    W.SafeDistance     = Attr.Combat.Range * 0.8f;

    return W;
}

// [Tactical EQS] NPC 스탯/상태를 블랙보드의 EQS 파라미터에 반영합니다.
// ExecuteMove 호출 직전에 자동 실행되어, 타겟 반경/전술 가중치 등을 최신 스탯 기준으로 갱신합니다.
void UNPCActionComponent::UpdateEQSParams()
{
    ASmartNPCAIController* AICtrl = GetOwnerAIController();
    if (!AICtrl || !StateComponent) return;
    UBlackboardComponent* BB = AICtrl->GetBlackboardComponent();
    if (!BB) return;

    const FEQSWeights W = ComputeEQSWeights();
    BB->SetValueAsFloat(FName("EQS_SearchRadius"),     W.SearchRadius);
    BB->SetValueAsFloat(FName("EQS_CoverWeight"),      W.CoverWeight);
    BB->SetValueAsFloat(FName("EQS_DistanceWeight"),   W.DistanceWeight);
    BB->SetValueAsFloat(FName("EQS_AggressionWeight"), W.AggressionWeight);
    BB->SetValueAsFloat(FName("EQS_SafeDistance"),     W.SafeDistance);

    UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] EQS Params 갱신 - Radius:%.0f, Cover:%.2f, DistWt:%.2f, AggWt:%.2f"),
        W.SearchRadius, W.CoverWeight, W.DistanceWeight, W.AggressionWeight);
}

// [경로 A] LLM 지시 이동 — DefaultMoveQuery(SingleResult)로 목적지 인근 최적점 탐색.
// 전술 재배치(Perception 트리거)는 TryStartTacticalQueryForCombat → TacticalPositionsQuery(AllMatching+LLM) 경로 사용.
void UNPCActionComponent::ExecuteMove(FVector TargetLocation, AActor* TargetActor, EMoveType SpeedType, ETacticalMoveState TacticalState)
{
    // target_loc이 명시된 경우 EQS 없이 직접 이동 (디버그 명령, 전술 쿼리 결과 주입 등)
    if (!TargetLocation.IsNearlyZero())
    {
        BaseMove(TargetLocation, SpeedType);
        return;
    }

    // TargetActor가 있으면 해당 위치로 직접 이동
    if (TargetActor)
    {
        BaseMove(TargetActor->GetActorLocation(), SpeedType);
        return;
    }

    // 목적지 미지정 → EQS AllMatching + Python location_decision 파이프라인으로 위치 결정.
    // BB의 TargetActor를 컨텍스트로 사용 (없으면 빈 배열 — EQS가 주변 최적 위치 탐색).
    if (TacticalQueryState != ETacticalQueryState::Idle)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] ExecuteMove: 이미 EQS 파이프라인 진행 중 — 스킵"));
        return;
    }

    TArray<FVector> ContextLocs;
    if (ASmartNPCAIController* AICon = GetOwnerAIController())
    {
        if (UBlackboardComponent* BB = AICon->GetBlackboardComponent())
        {
            if (AActor* Target = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor)))
                ContextLocs.Add(Target->GetActorLocation());
        }
    }

    StartTacticalQuery(ContextLocs);
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] ExecuteMove: target_loc 없음 → EQS+LLM 파이프라인 시작"));
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

    // Dist/Cover는 호출 측이 후보당 1회 계산해 전달 — Cover(LineTrace)를 스코어러마다
    // 재계산하면 후보 수 × 적 수 × 3배 트레이스가 발생하므로 시그니처에서 차단.

    /** Safe 스코어: 멀수록, 엄폐할수록, HP 낮을수록 가중치 */
    float EvalSafeScore(float Dist, float Cover, float HpPct,
        float DistScale, float CoverBonus, float LOSPenalty, float LowHpBonus)
    {
        const bool bLOS   = (Cover < 0.5f);
        float Score = FMath::Min(Dist / 1500.f, 1.f) * DistScale;
        Score += Cover * CoverBonus;
        Score += bLOS ? -LOSPenalty : 0.f;
        Score += (1.f - HpPct) * LowHpBonus;
        return Score;
    }

    /** Aggressive 스코어: 가까울수록, LOS 있을수록 */
    float EvalAggressiveScore(float Dist, float Cover, float HpPct,
        float DistScale, float LOSBonus, float CoverPenalty, float HpBonus)
    {
        const bool bLOS   = (Cover < 0.5f);
        float Score = (1.f - FMath::Min(Dist / 1500.f, 1.f)) * DistScale;
        Score += bLOS ? LOSBonus : 0.f;
        Score += Cover * -CoverPenalty;
        Score += HpPct * HpBonus;
        return Score;
    }

    /** Optimal 스코어: 중간 거리 + LOS + 적당한 엄폐 */
    float EvalOptimalScore(float Dist, float Cover,
        float IdealDist, float DistRange, float CoverBonus, float LOSBonus)
    {
        const bool bLOS   = (Cover < 0.5f);
        const float DistScore = FMath::Max(0.f, 2.f - FMath::Abs(Dist - IdealDist) / FMath::Max(DistRange, 1.f));
        return DistScore + Cover * CoverBonus + (bLOS ? LOSBonus : 0.f);
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
    ++TacticalQueryGeneration; // 새 요청 시작 — 이전 generation 의 응답은 stale 로 무시됨

    UEnvQuery* QueryAsset = TacticalPositionsQuery ? TacticalPositionsQuery : DefaultMoveQuery;
    if (!QueryAsset)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] StartTacticalQuery - EQS 에셋 없음 → 쿨다운 후 재시도"));
        // 상태는 Idle 유지(아직 변경 전) — 쿨다운만 갱신해 트리거 폭주 없이 재시도 허용.
        if (UWorld* World = GetWorld())
        {
            LastTacticalQueryTime = World->GetTimeSeconds();
        }
        return;
    }

    UpdateEQSParams();
    TacticalQueryState = ETacticalQueryState::WaitingEQS;

    // AllMatching: 복수 후보 전부 반환
    FEnvQueryRequest QueryRequest(QueryAsset, GetOwner());

    // [Named Parameter 주입] 블랙보드 대신 C++에서 EQS 에셋으로 직접 값을 쏴줍니다.
    if (StateComponent)
    {
        const FEQSWeights W = ComputeEQSWeights();
        QueryRequest.SetFloatParam(TEXT("SearchRadius"),          W.SearchRadius);
        QueryRequest.SetFloatParam(TEXT("DistanceWeightParam"),   W.DistanceWeight);
        QueryRequest.SetFloatParam(TEXT("AggressionWeightParam"), W.AggressionWeight);
        QueryRequest.SetFloatParam(TEXT("CoverWeightParam"),      W.CoverWeight);
    }

    QueryRequest.Execute(EEnvQueryRunMode::AllMatching,
        this, &UNPCActionComponent::OnTacticalCandidatesDone);

    UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] %s - 전술 EQS 쿼리 시작 (적 %d명)"),
        *GetOwnerAgentID(), EnemyLocations.Num());

    if (UWorld* World = GetWorld())
    {
        LastTacticalQueryTime = World->GetTimeSeconds();
    }
}

void UNPCActionComponent::TryStartTacticalQueryForCombat(const TArray<FVector>& EnemyLocations)
{
    if (EnemyLocations.IsEmpty()) return;

    // 이동 중에는 쿼리 금지 — 도중에 목적지가 바뀌면 우왕좌왕하는 문제 방지
    if (bIsBusy && CurrentAction.ActionType == EAction::Move)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] %s - 전술쿼리 스킵: 이동 중"),
            *GetOwnerAgentID());
        return;
    }

    // 이미 진행 중이면 스킵
    if (TacticalQueryState != ETacticalQueryState::Idle)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] %s - 전술쿼리 스킵: 이미 진행 중 (state=%d)"),
            *GetOwnerAgentID(), static_cast<int32>(TacticalQueryState));
        return;
    }

    // 쿨다운 체크
    if (UWorld* World = GetWorld())
    {
        const float Now = World->GetTimeSeconds();
        const float Remaining = TacticalQueryCooldown - (Now - LastTacticalQueryTime);
        if (Remaining > 0.f)
            return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("[NPCAction] %s - Perception 트리거 전술 쿼리 (쿨다운 통과)"),
        *GetOwnerAgentID());
    StartTacticalQuery(EnemyLocations);
}

void UNPCActionComponent::OnTacticalCandidatesDone(TSharedPtr<FEnvQueryResult> Result)
{
    if (!Result || !Result->IsSuccessful() || Result->Items.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] %s: EQS 결과 없음 (Result=%s, Items=%d) → 폴백. TacticalPositionsQuery 에셋 할당 및 NavMesh 커버리지 확인 필요."),
            *GetOwnerAgentID(),
            Result ? (Result->IsSuccessful() ? TEXT("Success") : TEXT("Failed")) : TEXT("Null"),
            Result ? Result->Items.Num() : -1);
        TacticalQueryState = ETacticalQueryState::Idle;

        // 폴백: 적 방향으로 직접 Move 액션 enqueue
        if (!CachedEnemyLocations.IsEmpty())
        {
            FGameAction MoveAction;
            MoveAction.ActionType = EAction::Move;
            MoveAction.Parameters.Add(NPCActionKeys::Key_TargetLoc, CachedEnemyLocations[0].ToString());
            ActionQueue.Enqueue(MoveAction);
            LastQueuedActionType = EAction::Move;
        }
        return;
    }

    UWorld* World = GetWorld();
    AActor* Owner = GetOwner();
    const float HpPct = StateComponent
        ? StateComponent->GetAttributes().Resources.GetHealthPercent()
        : 0.5f;

    // ── 스코어링 ─────────────────────────────────────────────────────────────
    // Fear/Aggression 스탯을 스코어 배율로 반영 (0~100 → 0.5~1.5 범위)
    float FearMult = 1.f, AggrMult = 1.f;
    if (StateComponent)
    {
        const FBehavioralTraits& B = StateComponent->GetAttributes().Behavior;
        FearMult = FMath::Clamp(0.5f + B.Fear * 0.01f, 0.5f, 1.5f);
        AggrMult = FMath::Clamp(0.5f + B.Aggression * 0.01f, 0.5f, 1.5f);
    }

    TArray<FLocationCandidate> AllCandidates;
    AllCandidates.Reserve(Result->Items.Num());

    for (int32 i = 0; i < Result->Items.Num(); ++i)
    {
        const FVector Loc = Result->GetItemAsLocation(i);

        const float Dist  = CalcDistToNearestEnemy(Loc, CachedEnemyLocations);
        const float Cover = CalcCoverRating(Loc, CachedEnemyLocations, World, Owner);

        const float SafeScore  = EvalSafeScore(Dist, Cover, HpPct,
            Score_SafeDistScale * FearMult, Score_CoverBonus * FearMult, Score_LOSPenalty, Score_LowHpFleeBonus * FearMult);
        const float AggrScore  = EvalAggressiveScore(Dist, Cover, HpPct,
            Score_AggrDistScale * AggrMult, Score_AggrLOSBonus * AggrMult, Score_AggrCoverPenalty, Score_AggrHpBonus * AggrMult);
        const float OptScore   = EvalOptimalScore(Dist, Cover,
            Score_OptIdealDist, Score_OptDistRange, Score_OptCoverBonus, Score_OptLOSBonus);

        UE_LOG(LogTemp, Verbose,
            TEXT("[EQS Score] Loc=(%.0f,%.0f,%.0f) Safe=%.2f(x%.1f) Aggr=%.2f(x%.1f) Opt=%.2f"),
            Loc.X, Loc.Y, Loc.Z, SafeScore, FearMult, AggrScore, AggrMult, OptScore);

        ELocationCategory BestCat;
        float BestScore;
        if (SafeScore >= AggrScore && SafeScore >= OptScore) { BestCat = ELocationCategory::Safe;       BestScore = SafeScore; }
        else if (AggrScore >= SafeScore && AggrScore >= OptScore) { BestCat = ELocationCategory::Aggressive; BestScore = AggrScore; }
        else { BestCat = ELocationCategory::Optimal; BestScore = OptScore; }

        FLocationCandidate Cand;
        Cand.Category       = BestCat;
        Cand.Location       = Loc;
        Cand.Score          = BestScore;
        Cand.DistanceToEnemy = Dist;
        Cand.CoverRating    = Cover;
        Cand.HeightDelta    = CachedEnemyLocations.IsEmpty() ? 0.f
            : Loc.Z - CachedEnemyLocations[0].Z;
        AllCandidates.Add(Cand);
    }

    // ── 카테고리별 Top-1 추리기 ───────────────────────────────────────────────
    // 카테고리당 최고 점수 1개만 Python에 전송 — Safe/Aggressive/Optimal 각 1개, 최대 3개.
    TMap<ELocationCategory, TArray<FLocationCandidate*>> ByCategory;
    for (FLocationCandidate& C : AllCandidates)
        ByCategory.FindOrAdd(C.Category).Add(&C);

    TArray<FLocationCandidate> Pruned;
    for (auto& KV : ByCategory)
    {
        KV.Value.Sort([](const FLocationCandidate& A, const FLocationCandidate& B){ return A.Score > B.Score; });
        if (KV.Value.Num() > 0)
        {
            FLocationCandidate C = *KV.Value[0];
            C.CandidateId = CategoryToString(C.Category); // "SAFE" / "AGGRESSIVE" / "OPTIMAL"
            Pruned.Add(C);
        }
    }

    if (Pruned.IsEmpty())
    {
        // 쿨다운은 StartTacticalQuery에서 이미 갱신됨 — Idle 복귀만으로 재시도 게이트 충분.
        TacticalQueryState = ETacticalQueryState::Idle;
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
    // Python 은 이 값을 그대로 응답에 echo. UE5 는 응답 처리 시 현재 generation 과 비교해 stale 차단.
    Payload->SetNumberField(TEXT("request_gen"),     static_cast<double>(TacticalQueryGeneration));

    FString PayloadStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

    const FString Envelope = FEnvelopeBuilder::BuildLocationDecisionRequest(PayloadStr);

    // 후보 시각화 — Manager/서버 연결 여부와 무관하게 항상 실행
    DrawEQSCandidates(Pruned, EQSDebugDuration);

    // ── LLMClient로 전송 ─────────────────────────────────────────────────────
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->SendEnvelopePromptToLLM(Envelope);
        TacticalQueryState = ETacticalQueryState::WaitingLLM;
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s - location_decision 전송 (후보 %d개)"),
            *AgentID, Pruned.Num());

        // LLM 무응답 시 자동 복구 타이머
        if (UWorld* W = GetWorld())
        {
            TWeakObjectPtr<UNPCActionComponent> WeakThis(this);
            W->GetTimerManager().SetTimer(TacticalLLMTimeoutTimer,
                [this, WeakThis]() { if (WeakThis.IsValid()) AbortTacticalQuery(); },
                TacticalLLMTimeout, false);
        }

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
        TacticalQueryState = ETacticalQueryState::Idle; // World/NPCManager 없음 — 고착 방지
    }
}

void UNPCActionComponent::NotifyLocationDecisionReady(const FString& ChosenCandidateId, const FString& Reason, uint32 RequestGen)
{
    // Stale 응답 차단 — Start/Abort 사이에 도착한 옛 generation 응답은 무시
    if (RequestGen != 0 && RequestGen != TacticalQueryGeneration)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[NPCAction] %s: stale location_decision 무시 (recv_gen=%u current_gen=%u id=%s)"),
            *GetOwnerAgentID(), RequestGen, TacticalQueryGeneration, *ChosenCandidateId);
        return;
    }

    // LLM 응답이 정상 도착했으므로 타임아웃 타이머 해제
    if (UWorld* W = GetWorld())
        W->GetTimerManager().ClearTimer(TacticalLLMTimeoutTimer);

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
            TacticalQueryState = ETacticalQueryState::Idle; // 후보 맵 비어있음 — 고착 방지
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
    MoveAction.Parameters.Add(NPCActionKeys::Key_TargetLoc, TacticalQueryResult.ToString());
    ActionQueue.Enqueue(MoveAction);

    // 큐 적재 완료 → 즉시 Idle로 복귀. ResultReady로 두면 다음 EQS 요청이 영구 스킵됨.
    TacticalQueryState = ETacticalQueryState::Idle;
    
    DrawEQSChosenLocation(TacticalQueryResult, ChosenCandidateId, Reason, EQSDebugDuration);

    // 최종 선택 좌표 표준 로그 (구조 통일)
    UE_LOG(LogTemp, Log,
        TEXT("[EQS RESULT] AgentID=%s | ChosenID=%s | X=%.1f Y=%.1f Z=%.1f"),
        *GetOwnerAgentID(), *ChosenCandidateId,
        TacticalQueryResult.X, TacticalQueryResult.Y, TacticalQueryResult.Z);
}

void UNPCActionComponent::AbortTacticalQuery()
{
    if (UWorld* W = GetWorld())
        W->GetTimerManager().ClearTimer(TacticalLLMTimeoutTimer);

    if (TacticalQueryState != ETacticalQueryState::Idle)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NPCAction] %s: TacticalQuery 중단 (state=%d gen=%u) → Idle 복구 (채널 비움)"),
            *GetOwnerAgentID(), static_cast<int32>(TacticalQueryState), TacticalQueryGeneration);

        // 상태만 Idle로 풀어 EventReport 채널을 다시 열어준다.
        // gen 증가·CandidateMap 비우기는 하지 않는다 — 늦게 도착하는 같은 gen 응답을
        // NotifyLocationDecisionReady가 여전히 해석할 수 있게 둔다(타임아웃≠무효).
        // 무효화는 다음 StartTacticalQuery의 ++gen이 책임진다(새 쿼리만 이전 응답을 stale 처리).
        TacticalQueryState = ETacticalQueryState::Idle;

        // 응답 안 와도 새 EQS 가 곧바로 재시작되는 무한 루프 방지(hearing 빈도 > LLM 응답).
        if (UWorld* World = GetWorld())
        {
            LastTacticalQueryTime = World->GetTimeSeconds();
        }
    }
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
    // UseItem 이 회복 효과 적용까지 담당 — 실패(미보유·비소비템)면 몽타주도 재생하지 않는다.
    if (InventoryComponent && InventoryComponent->UseItem(ItemID))
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

    // 공격 판정용 타겟 저장 — AM_Attack 의 AnimNotifyState_NPCAttackHit 윈도우가 이 타겟만 타격.
    if (ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetOwner()))
        OwnerNPC->SetCurrentAttackTarget(TargetActor);

    // 타겟 추적 이동(스냅샷 좌표 아님) — 사거리(AcceptanceRadius) 이내 도달 시
    // OnMoveActionCompleted 가 PendingMoveMediaKey("Attack") 몽타주를 재생한다.
    // 추격이 끝없이 길어지면 MaxActionDuration 워치독이 강제 완료 → 셀렉터 재선택.
    PendingMoveMediaKey = TEXT("Attack");
    BaseMoveToActor(TargetActor, EMoveType::Run, 150.f);

    if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
        UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_Attack, OwnerCharacter, 0.f, NPCActionKeys::NoiseTag_Attack);
}

// 이동 완료 공통 콜백: 도착 후 PendingMoveMediaKey가 있으면 몽타주 재생(완료는 몽타주 종료가 처리),
// 없거나 몽타주가 없으면 즉시 완료. 실패/취소(Aborted)에도 완료시켜 큐를 풀어준다.
void UNPCActionComponent::OnMoveActionCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
    // 콜백 중복 방지 — 바인딩 해제
    if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
        if (AAIController* AIC = Cast<AAIController>(OwnerCharacter->GetController()))
            if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
                PFC->OnRequestFinished.RemoveAll(this);

    if (Result.IsSuccess() && !PendingMoveMediaKey.IsEmpty())
    {
        const FString MediaKey = PendingMoveMediaKey;
        PendingMoveMediaKey.Reset();

        // 재생 성공 시 종료 콜백이 OnActionCompleted 호출(자세 플래그도 헬퍼가 재생 성공 시에만 set).
        // 미디어 미등록이면 여기서 즉시 완료.
        if (!PlayActionMediaWithPosture(MediaKey))
        {
            OnActionCompleted();
        }
        return;
    }

    PendingMoveMediaKey.Reset();
    OnActionCompleted();
}

void UNPCActionComponent::OnMontageActionEnded(UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
{
    // 몽타주 종료(정상/중단 모두) → 액션 완료. 가드가 중복/경합을 막는다.
    OnActionCompleted();
}

void UNPCActionComponent::HandleActionWatchdog()
{
    UE_LOG(LogTemp, Warning,
        TEXT("[NPCAction] %s: 액션 '%s' 완료 신호 미수신 — %.1f초 워치독 만료로 강제 완료"),
        *GetOwnerAgentID(), *UEnum::GetValueAsString(CurrentAction.ActionType), MaxActionDuration);
    OnActionCompleted();
}

void UNPCActionComponent::ExecuteBlock(AActor* TargetActor)
{
    if (TargetActor) ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    BasePlayActionMedia(TEXT("Block"));
}

void UNPCActionComponent::ExecuteDodgeAction(FVector Direction)
{
    // 셀렉터/LLM 이 주는 방향은 비정규(길이 임의) — 수평 단위벡터로 통일
    FVector Dir = Direction.GetSafeNormal2D();
    if (Dir.IsNearlyZero()) Dir = GetOwner()->GetActorForwardVector();

    ExecuteTurnTo(GetOwner()->GetActorLocation() + Dir * 100.f, nullptr);

    // 몽타주가 실제 재생될 때만 이동 — 미등록 시 기존 즉시 완료 경로 유지(모션 없는 순간이동 방지)
    if (BasePlayActionMedia(TEXT("Dodge")))
    {
        StartDodgeMove(Dir);
    }
}

void UNPCActionComponent::StartDodgeMove(const FVector& Direction)
{
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    UCharacterMovementComponent* MovementComp = OwnerCharacter ? OwnerCharacter->GetCharacterMovement() : nullptr;
    if (!MovementComp) return;

    // 진행 중 MoveTo 잔재가 회피 방향과 경합하지 않게 정지
    if (ASmartNPCAIController* AI = GetOwnerAIController())
    {
        AI->StopMovement();
    }

    // 마찰·제동 0 → Launch 속도가 몽타주 동안 감쇠 없이 유지(등속). MaxWalkSpeed 는 입력 가속에만
    // 적용되므로 건드릴 필요 없음. 원복은 StopDodgeMove(ClearActiveActionState 경유) 단일 경로.
    SavedGroundFriction        = MovementComp->GroundFriction;
    SavedBrakingDecelWalking   = MovementComp->BrakingDecelerationWalking;
    SavedBrakingFrictionFactor = MovementComp->BrakingFrictionFactor;
    MovementComp->GroundFriction             = 0.f;
    MovementComp->BrakingDecelerationWalking = 0.f;
    MovementComp->BrakingFrictionFactor      = 0.f;
    bDodgeMoveActive = true;

    // 모션-이동 일치: 구르기 몽타주는 전방 기준인데 Launch 는 입력 가속이 없어
    // bOrientRotationToMovement 가 회전을 안 돌림(SetFocalPoint 도 bUseControllerRotationYaw=false 라 무력).
    // → 액터 회전을 회피 방향으로 즉시 스냅.
    OwnerCharacter->SetActorRotation(Direction.Rotation());

    const float DodgeSpeed = ParseMoveSpeed(EMoveType::Run) * DodgeSpeedMultiplier;
    OwnerCharacter->LaunchCharacter(Direction * DodgeSpeed, true, false); // Z 미오버라이드 — 중력 유지

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Dodge 이동 시작 — 방향 %s, 속도 %.0f"),
        *GetOwnerAgentID(), *Direction.ToCompactString(), DodgeSpeed);
}

void UNPCActionComponent::StopDodgeMove()
{
    if (!bDodgeMoveActive) return;
    bDodgeMoveActive = false;

    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    UCharacterMovementComponent* MovementComp = OwnerCharacter ? OwnerCharacter->GetCharacterMovement() : nullptr;
    if (!MovementComp) return;

    MovementComp->GroundFriction             = SavedGroundFriction;
    MovementComp->BrakingDecelerationWalking = SavedBrakingDecelWalking;
    MovementComp->BrakingFrictionFactor      = SavedBrakingFrictionFactor;

    // 마찰 원복만으론 몇 프레임 더 미끄러짐 — 수평 잔류 속도 즉시 제거(낙하 Z 는 유지)
    MovementComp->Velocity.X = 0.f;
    MovementComp->Velocity.Y = 0.f;
}

void UNPCActionComponent::ExecuteFlee(FVector EscapeLocation)   { BaseMove(EscapeLocation, EMoveType::Run); }
void UNPCActionComponent::ExecuteSignalAllies(const FString& HandSign) { BaseSignalAllies(HandSign); }

// ==========================================
// [전투 행동 셀렉터] — SPEC_combat_selector Phase 1
// ==========================================

bool UNPCActionComponent::HasNearbyAlly(const AActor* EnemyTarget) const
{
    const AActor* Owner = GetOwner();
    if (!Owner || !StateComponent) return false;

    UNPCManager* Mgr = UNPCManager::Get(Owner);
    if (!Mgr) return false;

    for (const auto& Pair : Mgr->GetActiveNPCs())
    {
        const ASmartNPC* Other = Pair.Value;
        if (!Other || Other == Owner || Other == EnemyTarget || Other->bIsDead) continue;
        if (FVector::Dist(Owner->GetActorLocation(), Other->GetActorLocation()) > SignalAlliesRadius) continue;
        // 적대(배율 1.0)만 제외 — 중립·아군 모두 신호 대상 후보(구조 요청).
        if (StateComponent->GetAffinityMultiplier(Pair.Key) >= 1.f) continue;
        return true;
    }
    return false;
}

void UNPCActionComponent::ResetCombatSelectorState()
{
    LastCombatChoice = EAction::Idle;
    ConsecutiveCombatChoiceCount = 0;
    LastCombatSelectTime = -1000.f;
    bSignaledAlliesThisCombat = false;
}

bool UNPCActionComponent::SelectCombatAction(AActor* TargetActor)
{
    AActor* Owner = GetOwner();
    UWorld* World = GetWorld();
    if (!TargetActor || !Owner || !World || !StateComponent) return false;

    // 페이싱 — 액션 사이 최소 간격. 미충족 시 이번 틱은 대기(숨고르기).
    const float Now = World->GetTimeSeconds();
    if (Now - LastCombatSelectTime < CombatActionInterval) return false;

    const FNPCAttributes Attr = StateComponent->GetAttributes();
    const float Dist = FVector::Dist(Owner->GetActorLocation(), TargetActor->GetActorLocation());
    const float HPLoss = 1.f - FMath::Clamp(Attr.Resources.GetHealthPercent(), 0.f, 1.f);
    const bool bRecentlyHit = StateComponent->LastHitTime > 0.f
        && (Now - StateComponent->LastHitTime) < RecentHitWindow;

    // 방어 행동 공통 배율: 최근 피격 부스트 × 저HP 급증(하드 임계 없음 — 연속 스케일).
    const float DefenseBoost = (bRecentlyHit ? RecentHitDefenseBoost : 1.f) * (1.f + LowHPDefenseScale * HPLoss);
    // 방어 행동은 근접에서만 유의미 — 밖이면 급감.
    const float DefenseRangeFactor = (Dist <= DefenseReactRange) ? 1.f : 0.1f;

    struct FCombatCandidate { EAction Action; float Weight; };
    TArray<FCombatCandidate, TInlineAllocator<6>> Candidates;

    // Attack — Strength 파생. 연속 상한 도달 시 0(다른 행동 강제).
    {
        float W = CombatWeight_Attack * (Attr.BaseStats.Strength / CombatStatNorm);
        if (LastCombatChoice == EAction::Attack && ConsecutiveCombatChoiceCount >= MaxConsecutiveAttacks)
        {
            W = 0.f;
        }
        Candidates.Add({ EAction::Attack, W });
    }

    // Dodge — Agility 파생 × 방어 배율, 근접 한정.
    Candidates.Add({ EAction::Dodge,
        CombatWeight_Dodge * (Attr.BaseStats.Agility / CombatStatNorm) * DefenseBoost * DefenseRangeFactor });

    // Block — 방어 배율, 근접 한정(스탯 파생 없음 — 자세 유지형).
    Candidates.Add({ EAction::Block, CombatWeight_Block * DefenseBoost * DefenseRangeFactor });

    // 거리조절(Move) — Agility 파생 × 이상 링 이탈 정도. 링 안이면 0.
    {
        float SpacingUrge = 0.f;
        if (Dist < SpacingMinRange)
        {
            SpacingUrge = (SpacingMinRange - Dist) / SpacingMinRange;                      // 너무 붙음 → 백스텝
        }
        else if (Dist > SpacingMaxRange)
        {
            SpacingUrge = FMath::Min(1.f, (Dist - SpacingMaxRange) / SpacingMaxRange);     // 너무 멂 → 접근
        }
        Candidates.Add({ EAction::Move,
            CombatWeight_Spacing * (Attr.BaseStats.Agility / CombatStatNorm) * SpacingUrge });
    }

    // Flee — 저HP 제곱 램프 × 겁 성향(Fear↑·Bravery↓ → 0~2). 만HP≈0(가중치 급증은 저HP에서만).
    {
        const float CowardScale = (Attr.Behavior.Fear + (100.f - Attr.Behavior.Bravery)) / 100.f;
        Candidates.Add({ EAction::Flee, CombatWeight_Flee * LowHPFleeScale * HPLoss * HPLoss * CowardScale });
    }

    // SignalAllies — 아군 감지 시 확률 편입, 전투당 1회, 저HP 편향.
    if (!bSignaledAlliesThisCombat && HasNearbyAlly(TargetActor))
    {
        Candidates.Add({ EAction::SignalAllies, CombatWeight_Signal * (0.3f + HPLoss) });
    }

    // 연속 동일 행동 페널티(Attack 하드캡과 별개, 전 행동 공통 — 거듭제곱 누적).
    for (FCombatCandidate& C : Candidates)
    {
        if (C.Action == LastCombatChoice && ConsecutiveCombatChoiceCount > 0)
        {
            C.Weight *= FMath::Pow(CombatRepeatPenalty, static_cast<float>(ConsecutiveCombatChoiceCount));
        }
    }

    // 가중치 확률 추첨. 부동소수 잔여로 못 고르면 마지막 유효 후보.
    auto PickWeighted = [&Candidates]() -> int32
    {
        float TotalW = 0.f;
        for (const FCombatCandidate& C : Candidates) TotalW += FMath::Max(0.f, C.Weight);
        if (TotalW <= KINDA_SMALL_NUMBER) return INDEX_NONE;

        float Roll = FMath::FRandRange(0.f, TotalW);
        for (int32 i = 0; i < Candidates.Num(); ++i)
        {
            const float W = FMath::Max(0.f, Candidates[i].Weight);
            if (W <= 0.f) continue;
            if (Roll < W) return i;
            Roll -= W;
        }
        for (int32 i = Candidates.Num() - 1; i >= 0; --i)
        {
            if (Candidates[i].Weight > 0.f) return i;
        }
        return INDEX_NONE;
    };

    int32 ChosenIdx = PickWeighted();
    if (ChosenIdx == INDEX_NONE)
    {
        // 후보 전멸(사실상 도달 불가) — 이번 간격은 소진시켜 매 틱 재추첨 스핀 방지.
        LastCombatSelectTime = Now;
        return false;
    }

    // Flee 발동 주사위 — 배짱(Bravery) 체크 성공 시 도주 억제 후 재선택("용감한 놈 끝까지").
    if (Candidates[ChosenIdx].Action == EAction::Flee)
    {
        FDiceResult BraverySave;
        if (UDiceSystem::CheckReflex(Attr.Behavior.Bravery, FleeBraveryDifficulty, BraverySave))
        {
            UE_LOG(LogTemp, Log, TEXT("[CombatSelector] %s: 배짱 주사위 성공(roll %.0f < %.0f) — 도주 억제, 재선택"),
                *GetOwnerAgentID(), BraverySave.RollValue, BraverySave.TargetValue);
            Candidates[ChosenIdx].Weight = 0.f;
            const int32 Retry = PickWeighted();
            if (Retry != INDEX_NONE) ChosenIdx = Retry; // 대안 없으면 Flee 유지
        }
    }

    const EAction Chosen = Candidates[ChosenIdx].Action;

    FGameAction Action;
    Action.ActionType = Chosen;
    switch (Chosen)
    {
    case EAction::Attack:
        Action.FacialState = EFacialState::Angry;
        Action.Parameters.Add(NPCActionKeys::Key_TargetID, TargetActor->GetName());
        break;

    case EAction::Dodge:
    {
        // 타겟 기준 좌/우 측면 스텝 — ExecuteDodgeAction 이 이 방향으로 TurnTo 후 몽타주 + 등속 이동.
        const FVector ToTarget = (TargetActor->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
        FVector Lateral = FVector::CrossProduct(ToTarget, FVector::UpVector) * (FMath::RandBool() ? 1.f : -1.f);
        if (Lateral.IsNearlyZero()) Lateral = -Owner->GetActorForwardVector();
        Action.Parameters.Add(NPCActionKeys::Key_Direction, Lateral.ToString());
        break;
    }

    case EAction::Block:
        Action.Parameters.Add(NPCActionKeys::Key_TargetID, TargetActor->GetName());
        break;

    case EAction::Move:
    {
        // 계산 목적지로 BaseMove 직접(EQS 미사용) — 타겟 기준 자기쪽 Ideal 링 위 지점.
        FVector AwayDir = (Owner->GetActorLocation() - TargetActor->GetActorLocation()).GetSafeNormal2D();
        if (AwayDir.IsNearlyZero()) AwayDir = -Owner->GetActorForwardVector();
        const FVector Dest = TargetActor->GetActorLocation() + AwayDir * SpacingIdealRange;
        Action.Parameters.Add(NPCActionKeys::Key_TargetLoc, Dest.ToString());
        Action.Parameters.Add(NPCActionKeys::Key_Style, TEXT("Run"));
        break;
    }

    case EAction::Flee:
        // 위치 미지정 → ExecuteInteraction Flee 경로가 패닉 주사위 + EQS 후퇴로 처리.
        Action.FacialState = EFacialState::Fear;
        Action.Parameters.Add(NPCActionKeys::Key_TargetID, TargetActor->GetName());
        break;

    case EAction::SignalAllies:
        // target_id 가 미디어 키 겸용(ExecuteSignalAllies → BasePlayActionMedia). 미매핑 시 즉시 완료 폴백.
        Action.Parameters.Add(NPCActionKeys::Key_TargetID, TEXT("SignalAllies"));
        bSignaledAlliesThisCombat = true;
        break;

    default:
        break;
    }

    ActionQueue.Enqueue(Action);

    // 연속성 추적 갱신.
    if (Chosen == LastCombatChoice)
    {
        ++ConsecutiveCombatChoiceCount;
    }
    else
    {
        LastCombatChoice = Chosen;
        ConsecutiveCombatChoiceCount = 1;
    }
    LastCombatSelectTime = Now;

    // 선택 분포 로그(완료 기준 1 검증용) — 후보별 최종 가중치 나열.
    FString WeightStr;
    for (const FCombatCandidate& C : Candidates)
    {
        WeightStr += FString::Printf(TEXT("%s=%.2f "), *UEnum::GetValueAsString(C.Action), C.Weight);
    }
    UE_LOG(LogTemp, Log, TEXT("[CombatSelector] %s → %s (dist=%.0f hpLoss=%.0f%% recentHit=%d) W[ %s]"),
        *GetOwnerAgentID(), *UEnum::GetValueAsString(Chosen), Dist, HPLoss * 100.f, bRecentlyHit ? 1 : 0, *WeightStr);

    return true;
}

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
    if (!InventoryComponent || !InventoryComponent->HasItem(ItemID, Amount))
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCAction] 아이템 없음: %s"), *ItemID);
        return;
    }

    // 차감 전에 원본 데이터·수령처를 모두 확보 — 하나라도 없으면 건드리지 않는다(증발 방지).
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    UItemManager* ItemManager = GI ? GI->GetSubsystem<UItemManager>() : nullptr;

    FItemData Data;
    if (!ItemManager || !ItemManager->GetItemDataByID(ItemID, Data))
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCAction] 전달 중단 — 아이템 데이터 없음: %s"), *ItemID);
        return;
    }

    UInventoryComponent* ReceiverInv = ResolveReceiverInventory(TargetActor);
    if (!ReceiverInv)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 전달 중단 — 수령 대상 인벤토리 없음: %s"), *ItemID);
        return;
    }

    // 수령 실패(무게·슬롯 초과)면 NPC 보유분을 그대로 두고 종료.
    if (!ReceiverInv->AddItem(Data, Amount))
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 전달 실패(수령 인벤 공간·무게 부족): %s"), *ItemID);
        return;
    }

    InventoryComponent->RemoveItem(ItemID, Amount);
    ExecuteTurnTo(FVector::ZeroVector, TargetActor);
    BasePlayActionMedia(TEXT("Give"));
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 전달: %s x%d"), *ItemID, Amount);
}

UInventoryComponent* UNPCActionComponent::ResolveReceiverInventory(AActor* TargetActor) const
{
    // 1순위 — LLM 이 지정한 대상 액터(NPC↔NPC 전달도 그대로 동작).
    if (IsValid(TargetActor))
    {
        if (UInventoryComponent* Inv = TargetActor->FindComponentByClass<UInventoryComponent>())
        {
            return Inv;
        }
    }

    // 2순위 — 대상 미지정/인벤 없음이면 플레이어 폰 폴백(GiveItem 은 대부분 플레이어 대상).
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (APawn* PlayerPawn = PC->GetPawn())
            {
                return PlayerPawn->FindComponentByClass<UInventoryComponent>();
            }
        }
    }
    return nullptr;
}

void UNPCActionComponent::ExecuteComfort(AActor* TargetActor) { BaseComfort(TargetActor); }

// HandObject = "손에 들어 보여주기" 연출 전용 — 소유권은 이동하지 않는다.
// 실제 아이템 이전은 GiveItem 이 담당(여기서 플레이어 인벤에 넣으면 아이템이 복제됨).
void UNPCActionComponent::ExecuteHandObject(const FString& ItemID)
{
    if (InventoryComponent && InventoryComponent->HasItem(ItemID))
    {
        InventoryComponent->EquipItem(ItemID);
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] 아이템 제시: %s"), *ItemID);
    }
    else { UE_LOG(LogTemp, Error, TEXT("[NPCAction] 아이템 없음(제시): %s"), *ItemID); }
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

    // 월드 스폰·ItemManager 등록·차감은 DropItem 이 일괄 처리한다.
    // 스폰이 실패하면 인벤토리를 건드리지 않으므로 여기서 되돌릴 것이 없다.
    if (!InventoryComponent->DropItem(TargetTemplateID, 1))
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] 드랍 실패: %s"), *TargetTemplateID);
        return;
    }

    BasePlayActionMedia(TEXT("Drop"));
    UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwnerCharacter->GetActorLocation(), NPCActionKeys::Noise_Drop, OwnerCharacter, 0.f);
    UE_LOG(LogTemp, Log, TEXT("[NPCAction] 드랍: %s"), *TargetTemplateID);
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

void UNPCActionComponent::ExecuteTrack(AActor* TargetActor)
{
    if (!TargetActor) return;

    // 기존 추적 타이머 초기화 후 새 대상 설정
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TrackTimer);
    }
    TrackedTarget = TargetActor;

    // 즉시 첫 이동 명령
    if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
    {
        if (AAIController* AICon = Cast<AAIController>(OwnerPawn->GetController()))
        {
            AICon->MoveToActor(TargetActor, 150.f);
        }
    }

    // 0.5초 간격으로 MoveToActor 재발행 (대상이 이동하는 경우 추적 유지)
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            TrackTimer,
            this, &UNPCActionComponent::UpdateTrackPosition,
            0.5f, true
        );
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Track 시작 → %s"), *GetOwnerAgentID(), *TargetActor->GetName());
}

void UNPCActionComponent::UpdateTrackPosition()
{
    AActor* Target = TrackedTarget.Get();
    if (!IsValid(Target))
    {
        if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(TrackTimer);
        TrackedTarget.Reset();
        UE_LOG(LogTemp, Log, TEXT("[NPCAction] %s: Track 대상 소멸 — 추적 중단"), *GetOwnerAgentID());
        return;
    }

    if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
    {
        if (AAIController* AICon = Cast<AAIController>(OwnerPawn->GetController()))
        {
            AICon->MoveToActor(Target, 150.f);
        }
    }
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
void UNPCActionComponent::ExecuteStandUp()
{
    // Sit/Sleep 의 짝 — 진입 시 **가구 타입**이 자세를 정했듯, 해제는 **현재 자세**가 몽타주를 정한다.
    // LLM 이 자세를 몰라도(프롬프트에 자세 필드 없음) 단일 StandUp 하나로 앉기·눕기 모두 해제된다.
    if (!StateComponent)
    {
        // 상태 컴포넌트 없으면 자세 개념 자체가 없음 — 즉시 완료(워치독 회피).
        return;
    }

    const bool bLie = StateComponent->bIsLie;
    const bool bSit = StateComponent->bIsSit;

    if (!bLie && !bSit)
    {
        // 서 있는데 일어나라는 지시 — 무동작. 가구 없는 Sit 방어와 동일 패턴으로,
        // bActionAwaitingAsync=false 라 ExecuteInteraction 말미가 즉시 완료 처리(큐 정상 진행).
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] %s: StandUp 무동작 — 앉거나 누운 상태가 아님"),
            *GetOwnerAgentID());
        return;
    }

    // 점유 가구 반납 — 자세와 동일 라이프사이클. BaseLieUp/BaseSitUp 은 플래그만 내리므로
    // 여기서 반납하지 않으면 가구가 영구 점유로 남아 다른 NPC 가 못 쓴다.
    ReleaseOccupiedFurniture();

    // 눕기가 앉기보다 우선 — 둘 다 서 있을 수 없는 조합이나 플래그가 어긋난 경우의 방어.
    // 완료는 BasePlayActionMedia → OnMontageActionEnded 비동기 체인.
    if (bLie) BaseLieUp();
    else      BaseSitUp();
}

void UNPCActionComponent::ExecuteLifestyleAction(EAction LifestyleType, AActor* TargetEntity, FVector Location, const FString& StringParam)
{
    const FVector Dest = TargetEntity ? TargetEntity->GetActorLocation() : Location;

    // 이동은 가구行(Sit/Sleep)만 — 도착 후 몽타주(PendingMoveMediaKey, ExecuteAttackAction 과 동일 완료 체인).
    // in-place 액션(Read/Pray/Dance/Sing/Emote)은 이동 없이 제자리 재생 + 대상 바라보기만.
    // (구버전: 전 타입 BaseMove 직후 몽타주 즉시 재생 → 이동 중 앉기/춤 sliding 글리치 — Gemini PR#17 R4)
    // 가구行 판정 — Sit/Sleep 은 **가구 타겟 필수**(무타겟·비가구·좌표만 = 무동작 방어).
    // Read(책상)·Pray(제단) 는 가구 없음 — 항상 in-place.
    const bool bSitOrSleep = (LifestyleType == EAction::Sit || LifestyleType == EAction::Sleep);
    AFurnitureActor* FurnitureTarget = bSitOrSleep ? Cast<AFurnitureActor>(TargetEntity) : nullptr;

    if (bSitOrSleep && !FurnitureTarget)
    {
        // 가구 없인 못 앉는다 — LLM 이 nearby_furniture 컨텍스트를 받고도 무타겟 Sit 을 낸 케이스.
        // bActionAwaitingAsync=false 상태라 ExecuteInteraction 말미가 즉시 완료 처리(큐 정상 진행).
        UE_LOG(LogTemp, Warning, TEXT("[NPCAction] %s: %s 무동작 — 가구 타겟 없음(target 미지정/비가구)"),
            *GetOwnerAgentID(), *UEnum::GetValueAsString(LifestyleType));
        return;
    }

    if (FurnitureTarget)
    {
        // 다른 가구로 갈아타는 경우 이전 점유 선(先)해제 — Stop/Abort 를 기다리지 않는다.
        ReleaseOccupiedFurniture();

        // 도착 시 스냅·점유는 PlayActionMediaWithPosture 가 소비.
        // BaseMove 가 AlreadyAtGoal 을 동기 처리할 수 있으므로 반드시 BaseMove 호출 전에 세팅.
        PendingFurnitureTarget = FurnitureTarget;

        // 자세 플래그(bIsSit/bIsLie)는 여기서 세우지 않는다 — 도착 후 몽타주 재생 시점
        // (OnMoveActionCompleted)에 세운다. 미리 세우면 이동 실패·중도 Abort 시 앉지도
        // 않았는데 플래그만 true 로 고착된다(Gemini PR#20 high).
        // 자세(몽타주)는 **가구 타입**이 결정 — LLM 이 Sit/Sleep 을 혼동해도(예: 침대에 Sit)
        // 가구에 맞는 자세로 교정. Seat=앉기(SitDown), Bed=눕기(LieDown).
        // bIsSit/bIsLie 도 도착 후 BasePlayActionMedia(L548)가 이 MediaKey 기준으로 세운다.
        PendingMoveMediaKey = (FurnitureTarget->FurnitureType == EFurnitureType::Bed)
            ? NPCActionKeys::Interact_LieDown
            : NPCActionKeys::Interact_SitDown;
        BaseMove(Dest, EMoveType::Walk);
        ExecuteTurnTo(Dest, TargetEntity);
        return;
    }

    if (!Dest.IsNearlyZero())
    {
        ExecuteTurnTo(Dest, TargetEntity); // in-place — 대상을 바라보기만, 이동 없음
    }

    switch (LifestyleType)
    {
        // Sit/Sleep 은 위에서 가구 타겟 필수 처리(무가구 = 무동작) — 여기 도달 불가.
        case EAction::Pray:  BasePlayActionMedia(TEXT("Pray")); break;
        case EAction::Read:  BasePlayActionMedia(TEXT("Read")); break;
        case EAction::Dance: BaseDance(StringParam); break;
        case EAction::Sing:  BaseSing(StringParam); break;
        case EAction::Emote: BaseEmote(StringParam); break;
        // 추가 Lifestyle 타입 확장이 필요하다면 여기에 분기를 추가합니다.
        default: break;
    }
}

