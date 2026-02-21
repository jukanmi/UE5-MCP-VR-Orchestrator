#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_Actions.h"
#include "BTTask_InvestigationAction.generated.h"

UCLASS()
class UE5_MCP_VR_API UBTTask_InvestigationAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_InvestigationAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Investigation")
	EInvestigationAction SubAction;
};
