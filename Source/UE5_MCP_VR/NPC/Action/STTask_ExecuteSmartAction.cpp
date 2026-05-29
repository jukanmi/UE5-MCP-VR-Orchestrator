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
    FInstanceDataType& Data = Context.GetInstanceData(*this);

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

    Data.CachedActionComp = ActionComp;

    AActor* TargetActor = nullptr;
    if (UBlackboardComponent* BB = AICon.GetBlackboardComponent())
    {
        TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
    }

    const FGameAction& Action = ActionComp->GetCurrentAction();
    // 실행만 발행. 즉시형 액션은 ExecuteInteraction 내부에서 OnActionCompleted(bIsBusy=false)까지 끝나고,
    // 비동기 액션(이동/몽타주)은 콜백이 완료시킬 때까지 bIsBusy=true가 유지된다.
    ActionComp->ExecuteInteraction(Action.ActionType, TargetActor, Action.Parameters);

    // 같은 프레임에 즉시형이 완료됐을 수 있으니 Tick으로 판정.
    return Tick(Context, 0.f);
}

EStateTreeRunStatus FSTTask_ExecuteSmartAction::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);

    UNPCActionComponent* ActionComp = Data.CachedActionComp.Get();
    if (!ActionComp) return EStateTreeRunStatus::Failed; // NPC pending destroy 등

    // 비동기 완료 대기 — 이동 도착/몽타주 종료/즉시형 완료/워치독 만료 시 bIsBusy=false.
    return ActionComp->bIsBusy ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}
