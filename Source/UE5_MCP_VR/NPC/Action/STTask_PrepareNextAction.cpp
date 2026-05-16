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

    // 큐 비어있음 — 자율 행동 주입 정책 (2026-05-16 변경):
    //   비전투 자동 Track 은 제거. Perception 으로 BB.TargetActor 가 채워졌다 해도
    //   LLM 이 명시한 액션이 없으면 NPC 는 Idle 유지한다. (사용자 결정)
    //   Combat 모드는 그대로 Attack 자동 주입 — 적 시야 진입 시 즉시 반응이 필요.
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    if (ActionComp->CurrentBehaviorMode == ENPCBehaviorMode::Combat)
    {
        if (UBlackboardComponent* BB = AICon.GetBlackboardComponent())
        {
            if (AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor)))
            {
                FGameAction AutoAction;
                AutoAction.ActionType = EAction::Attack;
                AutoAction.Parameters.Add(NPCActionKeys::Key_TargetID, TargetActor->GetName());
                ActionComp->ActionQueue.Enqueue(AutoAction);
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
