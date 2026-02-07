#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BTTask_MoveToLocation.generated.h"

/**
 * Custom MoveTo task that resets ActionType to Idle on completion
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_MoveToLocation : public UBTTask_MoveTo
{
	GENERATED_BODY()

public:
	UBTTask_MoveToLocation();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
};
