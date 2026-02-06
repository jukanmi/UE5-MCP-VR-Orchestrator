#include "BTTask_MoveToLocation.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTTask_MoveToLocation::UBTTask_MoveToLocation()
{
	NodeName = "Move To Location";
}

EBTNodeResult::Type UBTTask_MoveToLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return Super::ExecuteTask(OwnerComp, NodeMemory);
}

void UBTTask_MoveToLocation::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

	// Reset ActionType to Idle when movement succeeds
	if (TaskResult == EBTNodeResult::Succeeded)
	{
		if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
		{
			// ESmartNPCActionState::Idle = 0
			BB->SetValueAsEnum(FName("ActionType"), 0);
			UE_LOG(LogTemp, Log, TEXT("[MoveToLocation] Movement complete - ActionType reset to Idle"));
		}
	}
}
