#include "BTTask_CombatAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_CombatAction::UBTTask_CombatAction()
{
	NodeName = "Combat Action";
	SubAction = ECombatAction::Attack;
}

EBTNodeResult::Type UBTTask_CombatAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	// Read SubAction from Blackboard (string format)
	FString SubActionStr = BB->GetValueAsString(ASmartNPCAIController::Key_SubAction);
	FString ParamsJson = BB->GetValueAsString(ASmartNPCAIController::Key_ActionParameters);

	// Parse JSON parameters
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

	// Route to appropriate execution
	// Resolve Enum from String
	const UEnum* EnumPtr = StaticEnum<ECombatAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ECombatAction::%s"), *SubActionStr)));
	ECombatAction Action = (ECombatAction)EnumValue;

	// Route to appropriate execution based on Enum
	switch (Action)
	{
	case ECombatAction::Attack:
		{
			FString TargetID = Params.FindRef(TEXT("TargetID"));
			NPC->ExecuteAttack(TargetID);
		}
		break;
	
	default:
		{
			// Block, Dodge, Flee, UseCombatItem, SignalAllies → ExecuteGenericAction
			FString TargetID = Params.FindRef(TEXT("TargetID"));
			NPC->ExecuteGenericAction(SubActionStr, TargetID);
		}
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_CombatAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Combat Action: %s"), *UEnum::GetValueAsString(SubAction));
}
