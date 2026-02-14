#include "BTTask_SocialAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_SocialAction::UBTTask_SocialAction()
{
	NodeName = "Social Action";
	SubAction = ESocialAction::Emote;
}

EBTNodeResult::Type UBTTask_SocialAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
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
	const UEnum* EnumPtr = StaticEnum<ESocialAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ESocialAction::%s"), *SubActionStr)));
	ESocialAction Action = (ESocialAction)EnumValue;

	switch (Action)
	{

	case ESocialAction::Emote:
		{
			FString GestureType = Params.FindRef(TEXT("GestureType"));
			NPC->ExecuteEmote(GestureType);
		}
		break;
	case ESocialAction::Follow:
		{
			FString TargetID = Params.FindRef(TEXT("TargetID"));
			float Distance = FCString::Atof(*Params.FindRef(TEXT("Distance")));
			// Follow uses ExecuteMove with target location
			FVector TargetLoc = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
			NPC->ExecuteMove(TargetLoc, 200.0f);
		}
		break;
	
	default:
		// Trade, GiveItem, Comfort, HandObject → ExecuteGenericAction
		FString TargetID = Params.FindRef(TEXT("TargetID"));
		NPC->ExecuteGenericAction(SubActionStr, TargetID);
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_SocialAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Social Action: %s"), *UEnum::GetValueAsString(SubAction));
}
