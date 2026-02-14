#include "BTTask_LifestyleAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_LifestyleAction::UBTTask_LifestyleAction()
{
	NodeName = "Lifestyle Action";
	SubAction = ELifestyleAction::Sit;
}

EBTNodeResult::Type UBTTask_LifestyleAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
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
	const UEnum* EnumPtr = StaticEnum<ELifestyleAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ELifestyleAction::%s"), *SubActionStr)));
	ELifestyleAction Action = (ELifestyleAction)EnumValue;

	switch (Action)
	{
	case ELifestyleAction::Dance:
	case ELifestyleAction::Sing:
		// Use Emote system for expressive actions
		NPC->ExecuteEmote(SubActionStr);
		break;
	
	default:
		// Sit, Sleep, Clean, Read, Pray → ExecuteGenericAction
		FString TargetID = Params.FindRef(TEXT("ChairID"));
		if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("BedID"));
		if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("BookID"));
		if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("Area"));
		NPC->ExecuteGenericAction(SubActionStr, TargetID);
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_LifestyleAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Lifestyle Action: %s"), *UEnum::GetValueAsString(SubAction));
}
