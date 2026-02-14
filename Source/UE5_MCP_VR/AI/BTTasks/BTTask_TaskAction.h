#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_TaskActions.h"
#include "BTTask_TaskAction.generated.h"

UCLASS()
class UE5_MCP_VR_API UBTTask_TaskAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_TaskAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Task")
	ETaskAction SubAction;
};
