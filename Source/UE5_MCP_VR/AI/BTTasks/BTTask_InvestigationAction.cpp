#include "BTTask_InvestigationAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_InvestigationAction::UBTTask_InvestigationAction()
{
	NodeName = "Investigation Action";
	SubAction = EInvestigationAction::Investigate;
}

EBTNodeResult::Type UBTTask_InvestigationAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr = BB->GetValueAsString(ASmartNPCAIController::Key_SubAction);
	FString ParamsJson = BB->GetValueAsString(ASmartNPCAIController::Key_ActionParameters);

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJson);
	FJsonSerializer::Deserialize(Reader, JsonObj);

	TMap<FString, FString> Params;
	if (JsonObj.IsValid())
	{
		for (auto& Pair : JsonObj->Values)
		{
			Params.Add(Pair.Key, Pair.Value->AsString());
		}
	}

	// Resolve Enum from String
	const UEnum* EnumPtr = StaticEnum<EInvestigationAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("EInvestigationAction::%s"), *SubActionStr)));
	EInvestigationAction Action = (EInvestigationAction)EnumValue;

	switch (Action)
	{
	case EInvestigationAction::Investigate:
	case EInvestigationAction::Scout:
		{
			// Move to investigation location
			FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
			NPC->ExecuteMove(Location, 150.0f);
		}
		break;
	
	default:
		// Track → ExecuteGenericAction
		FString TargetID = Params.FindRef(TEXT("TargetID"));
		if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("TargetTrace"));
		NPC->ExecuteGenericAction(SubActionStr, TargetID);
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_InvestigationAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Investigation Action: %s"), *UEnum::GetValueAsString(SubAction));
}
