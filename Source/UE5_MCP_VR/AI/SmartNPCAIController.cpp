#include "SmartNPCAIController.h"
#include "SmartNPC.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

// Define Key Names
const FName ASmartNPCAIController::Key_TargetLocation(TEXT("TargetLocation"));
const FName ASmartNPCAIController::Key_ActionType(TEXT("ActionType"));
const FName ASmartNPCAIController::Key_TargetActor(TEXT("TargetActor"));
const FName ASmartNPCAIController::Key_SpeakText(TEXT("SpeakText"));

ASmartNPCAIController::ASmartNPCAIController()
{
}

void ASmartNPCAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (ASmartNPC* NPC = Cast<ASmartNPC>(InPawn))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SmartNPCAIController] Possessed NPC: %s"), *NPC->AgentID);
		
		// If the NPC has a defined Behavior Tree, initialize it
		if (NPC->BehaviorTreeAsset && NPC->BehaviorTreeAsset->BlackboardAsset)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SmartNPCAIController] Starting BT: %s with BB: %s"), 
				*NPC->BehaviorTreeAsset->GetName(),
				*NPC->BehaviorTreeAsset->BlackboardAsset->GetName());
			
			// Start Behavior Tree
			// RunBehaviorTree automatically initializes the Blackboard component
			bool bSuccess = RunBehaviorTree(NPC->BehaviorTreeAsset);
			UE_LOG(LogTemp, Warning, TEXT("[SmartNPCAIController] RunBehaviorTree result: %s"), 
				bSuccess ? TEXT("SUCCESS") : TEXT("FAILED"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[SmartNPCAIController] NPC %s has no BehaviorTreeAsset or BlackboardAsset!"), *NPC->AgentID);
		}
	}
}
