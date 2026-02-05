#include "SmartNPCAIController.h"
#include "SmartNPC.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

// Define Key Names
const FName ASmartNPCAIController::Key_TargetLocation(TEXT("TargetLocation"));
const FName ASmartNPCAIController::Key_ActionType(TEXT("ActionType"));
const FName ASmartNPCAIController::Key_TargetActor(TEXT("TargetActor"));

ASmartNPCAIController::ASmartNPCAIController()
{
}

void ASmartNPCAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (ASmartNPC* NPC = Cast<ASmartNPC>(InPawn))
	{
		// If the NPC has a defined Behavior Tree, initialize it
		if (NPC->BehaviorTreeAsset && NPC->BehaviorTreeAsset->BlackboardAsset)
		{
			// Start Behavior Tree
			// RunBehaviorTree automatically initializes the Blackboard component
			RunBehaviorTree(NPC->BehaviorTreeAsset);
		}
	}
}
