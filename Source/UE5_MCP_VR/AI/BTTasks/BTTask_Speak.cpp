#include "BTTask_Speak.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"

UBTTask_Speak::UBTTask_Speak()
{
	NodeName = "Speak";
}

EBTNodeResult::Type UBTTask_Speak::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC)
	{
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return EBTNodeResult::Failed;
	}

	// Get speech text from Blackboard
	FString SpeakText = BB->GetValueAsString(FName("SpeakText"));
	
	if (SpeakText.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BTTask_Speak] %s: No text to speak!"), *NPC->AgentID);
		return EBTNodeResult::Failed;
	}

	UE_LOG(LogTemp, Log, TEXT("[BTTask_Speak] %s says: \"%s\""), *NPC->AgentID, *SpeakText);

	// Reset ActionType to Idle after speaking
	BB->SetValueAsEnum(FName("ActionType"), 0); // Idle = 0
	
	// Clear the SpeakText
	BB->SetValueAsString(FName("SpeakText"), TEXT(""));

	return EBTNodeResult::Succeeded;
}
