#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_CombatActions.h"
#include "BTTask_CombatAction.generated.h"

/**
 * BTTaskNode for Combat mode actions.
 * Reads SubAction from Blackboard and executes corresponding combat behavior.
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_CombatAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CombatAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** Combat action to execute (set in BT editor or read from Blackboard) */
	UPROPERTY(EditAnywhere, Category = "Combat")
	ECombatAction SubAction;
};
