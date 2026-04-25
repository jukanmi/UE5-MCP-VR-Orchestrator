#include "BTTask_ExecuteSmartAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"

UBTTask_ExecuteSmartAction::UBTTask_ExecuteSmartAction()
{
	NodeName = TEXT("Execute Smart Action");
}

EBTNodeResult::Type UBTTask_ExecuteSmartAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ASmartNPCAIController* AICon = Cast<ASmartNPCAIController>(OwnerComp.GetAIOwner());
	if (!AICon) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AICon->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
	
	// [의도(Why)] 파라미터(Type, Target, Params Map)의 라우팅 책임을 온전히 NPCActionComponent에 단일 위임(Delegate)
	// Blackboard를 경유한 JSON 직렬화/역직렬화 오버헤드를 방지하고 컴포넌트의 CurrentAction을 직접 참조합니다.
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        const FGameAction& Action = ActionComp->GetCurrentAction();
        ActionComp->ExecuteInteraction(Action.ActionType, TargetActor, Action.Parameters);
    }

	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString UBTTask_ExecuteSmartAction::GetStaticDescription() const
{
	return TEXT("단일화된 EAction 기반 NPC 명령을 싱글톤 라우팅합니다.");
}
