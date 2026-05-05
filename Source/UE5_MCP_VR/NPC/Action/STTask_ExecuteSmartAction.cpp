#include "STTask_ExecuteSmartAction.h"
#include "StateTreeLinker.h"
#include "StateTreeExecutionContext.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"

bool FSTTask_ExecuteSmartAction::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

EStateTreeRunStatus FSTTask_ExecuteSmartAction::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    ASmartNPC* NPC = Cast<ASmartNPC>(AICon.GetPawn());
    if (!NPC) return EStateTreeRunStatus::Failed;

    UNPCActionComponent* ActionComp = NPC->GetActionComponent();
    if (!ActionComp) return EStateTreeRunStatus::Failed;

    if (!ActionComp->bIsBusy)
    {
        UE_LOG(LogTemp, Warning, TEXT("[STTask_Execute] %s: no prepared action — skipping"), *NPC->GetName());
        return EStateTreeRunStatus::Failed;
    }

    AActor* TargetActor = nullptr;
    if (UBlackboardComponent* BB = AICon.GetBlackboardComponent())
    {
        TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
    }

    const FGameAction& Action = ActionComp->GetCurrentAction();
    ActionComp->ExecuteInteraction(Action.ActionType, TargetActor, Action.Parameters);

    NPC->OnActionCompleted();
    return EStateTreeRunStatus::Succeeded;
}
