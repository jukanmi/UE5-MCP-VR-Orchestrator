#include "BTTask_CommonAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_CommonAction::UBTTask_CommonAction()
{
	NodeName = "Common Action";
	SubAction = ECommonAction::Idle;
}

EBTNodeResult::Type UBTTask_CommonAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr = BB->GetValueAsString(ASmartNPCAIController::Key_SubAction);
	FString ParamsJson = BB->GetValueAsString(ASmartNPCAIController::Key_ActionParameters);

	// Parse Params JSON
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
	const UEnum* EnumPtr = StaticEnum<ECommonAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*SubActionStr));
	if (EnumValue == INDEX_NONE)
	{
		EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ECommonAction::%s"), *SubActionStr)));
	}
	
	ECommonAction Action = (EnumValue != INDEX_NONE) ? (ECommonAction)EnumValue : ECommonAction::Idle;
	SubAction = Action; // For debug display

	switch (Action)
	{
	case ECommonAction::Move:
	{
		FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
		float Speed = 300.0f;
		if (Params.Contains("Speed")) Speed = FCString::Atof(*Params["Speed"]);
		NPC->ExecuteMove(Location, Speed);
	}
	break;
	case ECommonAction::Follow:
		{
			FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
			float Speed = 300.0f;
			if (Params.Contains("Speed")) Speed = FCString::Atof(*Params["Speed"]);
			NPC->ExecuteFollow(Location, Speed);
		}
		break;

	case ECommonAction::Dialogue:
		{
			FString Content = Params.FindRef(TEXT("Content"));
			if (Content.IsEmpty()) Content = Params.FindRef(TEXT("Info")); // Fallback for Report
			NPC->ExecuteSpeak(Content);
		}
		break;

	case ECommonAction::UseItem:
		{
			FString ItemID = Params.FindRef(TEXT("ItemID"));
			if (ItemID.IsEmpty()) ItemID = Params.FindRef(TEXT("PotionID"));
			if (ItemID.IsEmpty()) ItemID = Params.FindRef(TEXT("WeaponID"));
			NPC->ExecuteGenericAction(SubActionStr, ItemID); 
		}
		break;

	case ECommonAction::TurnTo:
		{
			FString TargetID = Params.FindRef(TEXT("TargetID"));
			NPC->ExecuteGenericAction(TEXT("TurnTo"), TargetID);
		}
		break;

	case ECommonAction::Wait:
	case ECommonAction::Idle:
	default: 
		// Wait, Idle, Stop, Scan -> Generic
		NPC->ExecuteGenericAction(SubActionStr, TEXT(""));
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_CommonAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Common Action"));
}
