#include "BTTask_NotifyActionCompleted.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"

UBTTask_NotifyActionCompleted::UBTTask_NotifyActionCompleted()
{
	NodeName = TEXT("Notify Action Completed");
}

EBTNodeResult::Type UBTTask_NotifyActionCompleted::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ASmartNPCAIController* AICon = Cast<ASmartNPCAIController>(OwnerComp.GetAIOwner());
	if (!AICon) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AICon->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
	{
		ActionComp->OnActionCompleted();
		return EBTNodeResult::Succeeded;
	}

	return EBTNodeResult::Failed;
}

FString UBTTask_NotifyActionCompleted::GetStaticDescription() const
{
	return TEXT("NPCActionComponent에 Action 완료 신호를 보내, 큐에서 다음 Action을 처리할 수 있게 합니다.");
}
