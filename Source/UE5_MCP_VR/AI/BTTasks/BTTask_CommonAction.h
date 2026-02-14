#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_CommonActions.h"
#include "BTTask_CommonAction.generated.h"

/**
 * Common Behavior Mode BT Task
 * Handles general actions like Idle, Move, Dialogue, Wait, UseItem
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_CommonAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CommonAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Action")
	ECommonAction SubAction;
};
