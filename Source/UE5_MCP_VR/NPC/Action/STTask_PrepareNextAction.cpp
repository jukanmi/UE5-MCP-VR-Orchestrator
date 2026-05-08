#include "STTask_PrepareNextAction.h"
#include "StateTreeLinker.h"
#include "StateTreeExecutionContext.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"
#include "../Struct/NPCActionKeys.h"

bool FSTTask_PrepareNextAction::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

EStateTreeRunStatus FSTTask_PrepareNextAction::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);

    // EnterState 시 한 번만 Cast → InstanceData에 캐싱. 이후 Tick은 캐시된 포인터 사용.
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    if (ASmartNPC* NPC = Cast<ASmartNPC>(AICon.GetPawn()))
    {
        Data.CachedActionComp = NPC->GetActionComponent();
    }

    return Tick(Context, 0.f);
}

EStateTreeRunStatus FSTTask_PrepareNextAction::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    Data.bHasAction = false;

    UNPCActionComponent* ActionComp = Data.CachedActionComp.Get();
    if (!ActionComp) return EStateTreeRunStatus::Running; // NPC pending destroy 등

    // 이전 액션 진행 중이면 대기 — OnActionCompleted가 bIsBusy=false로 풀어줄 때까지.
    if (ActionComp->bIsBusy) return EStateTreeRunStatus::Running;

    // 큐에 액션 있으면 Dequeue → 다음 State로
    if (ActionComp->ProcessNextAction())
    {
        const FGameAction& Action = ActionComp->GetCurrentAction();
        Data.bHasAction = true;
        Data.SubAction = Action.ActionType;

        if (const FString* LocStr = Action.Parameters.Find(NPCActionKeys::Key_TargetLoc))
        {
            FVector Loc;
            if (!LocStr->IsEmpty() && Loc.InitFromString(*LocStr))
            {
                Data.TargetLocation = Loc;
            }
        }
        return EStateTreeRunStatus::Succeeded;
    }

    // 큐 비어있음 — BB에 TargetActor가 있으면 모드에 따라 자율 행동 주입.
    // Move: EQS는 Perception 이벤트에서만 발동. 여기서는 Attack/Follow 직접 주입.
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    if (UBlackboardComponent* BB = AICon.GetBlackboardComponent())
    {
        if (AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor)))
        {
            if (ActionComp->CurrentBehaviorMode == ENPCBehaviorMode::Combat)
            {
                // Combat: Attack 직접 주입 (ExecuteAttackAction이 이동+공격 일괄 처리)
                FGameAction AutoAction;
                AutoAction.ActionType = EAction::Attack;
                AutoAction.Parameters.Add(NPCActionKeys::Key_TargetID, TargetActor->GetName());
                ActionComp->ActionQueue.Enqueue(AutoAction);
            }
            else
            {
                // 비전투: Track 사용 — TrackTimer(0.5s 주기)가 MoveToActor를 지속 갱신하므로
                // 이미 같은 대상을 추적 중이면 재주입 생략 (Follow 완료→재시작 루프 방지)
                if (!ActionComp->IsTrackingTarget(TargetActor))
                {
                    FGameAction AutoAction;
                    AutoAction.ActionType = EAction::Track;
                    AutoAction.Parameters.Add(NPCActionKeys::Key_TargetID, TargetActor->GetName());
                    ActionComp->ActionQueue.Enqueue(AutoAction);
                }
            }
        }
    }

    return EStateTreeRunStatus::Running;
}

// ============================================================================
// FSTEvaluator_NPCState
// ============================================================================

bool FSTEvaluator_NPCState::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

void FSTEvaluator_NPCState::TreeStart(FStateTreeExecutionContext& Context) const
{
    Tick(Context, 0.0f);
}

void FSTEvaluator_NPCState::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FSTEvaluator_NPCState_InstanceData& InstanceData = Context.GetInstanceData(*this);
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);

    if (ASmartNPC* NPC = Cast<ASmartNPC>(AICon.GetPawn()))
    {
        if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
        {
            InstanceData.bHasAction = ActionComp->HasPendingActions() || ActionComp->bIsBusy;
            InstanceData.BehaviorMode = ActionComp->CurrentBehaviorMode;
        }
    }
}
