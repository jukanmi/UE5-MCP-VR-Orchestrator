#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_LifestyleActions.h"
#include "BTTask_LifestyleAction.generated.h"

UCLASS()
class UE5_MCP_VR_API UBTTask_LifestyleAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_LifestyleAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Lifestyle")
	ELifestyleAction SubAction;
};
