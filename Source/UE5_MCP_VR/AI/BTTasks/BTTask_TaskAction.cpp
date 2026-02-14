#include "BTTask_TaskAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_TaskAction::UBTTask_TaskAction()
{
	NodeName = "Task Action";
	SubAction = ETaskAction::PickUp;
}

EBTNodeResult::Type UBTTask_TaskAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
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
	const UEnum* EnumPtr = StaticEnum<ETaskAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ETaskAction::%s"), *SubActionStr)));
	ETaskAction Action = (ETaskAction)EnumValue;

	switch (Action)
	{
	case ETaskAction::UseObject:
		{
			FString ObjectID = Params.FindRef(TEXT("ObjectID"));
			NPC->ExecuteInteract(ObjectID);
		}
		break;
	
	default:
		// PickUp, Drop, Craft, Repair → ExecuteGenericAction
		FString TargetID = Params.FindRef(TEXT("ItemID"));
		if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("TargetObject"));
		if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("RecipeID"));
		NPC->ExecuteGenericAction(SubActionStr, TargetID);
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_TaskAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Task Action: %s"), *UEnum::GetValueAsString(SubAction));
}
