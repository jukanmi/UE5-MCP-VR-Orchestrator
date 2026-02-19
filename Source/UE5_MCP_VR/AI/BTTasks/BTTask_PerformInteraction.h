#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PerformInteraction.generated.h"

/**
 * [Refactor] Perform Interaction Task
 * 
 * A generalized BT Task that delegates execution to ASmartNPC::ExecuteInteraction.
 * Instead of maintaining hardcoded Enums for every new action, this task dynamically
 * reads the 'SubAction' key from Blackboard and forwards it to the NPC.
 * 
 * - Key_SubAction: The interaction type (e.g., "Sit", "Dance")
 * - Key_TargetActor: The target actor (physical object)
 * - Key_ActionParameters: JSON parameters for extra data (TargetID, Style, etc.)
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_PerformInteraction : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_PerformInteraction();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual FString GetStaticDescription() const override;
};
