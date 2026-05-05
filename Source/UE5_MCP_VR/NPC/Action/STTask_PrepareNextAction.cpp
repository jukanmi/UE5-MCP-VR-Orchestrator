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

    // 큐 비어있음 — BB에 TargetActor가 있으면 모드 무관하게 EQS+LLM 위치 결정 파이프라인 발동.
    // Combat: 적 위치 기반 전술 스코어링. Common/Social: TargetActor를 컨텍스트로 최적 위치 선택.
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    if (UBlackboardComponent* BB = AICon.GetBlackboardComponent())
    {
        if (AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor)))
        {
            TArray<FVector> ContextLocs;
            ContextLocs.Add(TargetActor->GetActorLocation());
            ActionComp->TryStartTacticalQueryForCombat(ContextLocs);
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
