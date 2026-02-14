#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_SocialActions.h"
#include "BTTask_SocialAction.generated.h"

UCLASS()
class UE5_MCP_VR_API UBTTask_SocialAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_SocialAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Social")
	ESocialAction SubAction;
};
