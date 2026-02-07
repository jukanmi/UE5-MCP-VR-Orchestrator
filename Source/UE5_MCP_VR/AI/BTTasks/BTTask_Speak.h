#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_Speak.generated.h"

/**
 * BTTask that displays NPC speech text
 * Reads SpeakText from Blackboard and shows it (via delegate or widget)
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_Speak : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_Speak();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/** Duration to display the text (seconds) */
	UPROPERTY(EditAnywhere, Category = "Speech")
	float DisplayDuration = 3.0f;
};
