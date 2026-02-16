#include "BTTask_SocialAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "../NPCActionKeys.h"
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
	int64 EnumValue = EnumPtr->GetValueByName(FName(*SubActionStr));
	if (EnumValue == INDEX_NONE)
	{
		// Try scoped
		EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ESocialAction::%s"), *SubActionStr)));
	}
	
	ESocialAction Action = (EnumValue != INDEX_NONE) ? (ESocialAction)EnumValue : ESocialAction::Emote; // Default
	SubAction = Action;

	switch (Action)
	{

	case ESocialAction::Emote:
		{
			FString GestureType = Params.FindRef(TEXT("GestureType"));
			if (GestureType.IsEmpty()) GestureType = Params.FindRef(TEXT("gesture"));
			NPC->ExecuteEmote(GestureType);
		}
		break;
	
	case ESocialAction::Follow:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			float Distance = 200.0f;
			if (Params.Contains(TEXT("distance"))) Distance = FCString::Atof(*Params[TEXT("distance")]);
			
            if (TargetActor)
            {
                NPC->ExecuteKeepDistance(TargetActor, Distance);
            }
            else
            {
                // Fallback to MoveToLocation if only location is known
			    FVector TargetLoc = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
			    NPC->ExecuteMoveToLocation(TargetLoc, ASmartNPC::EMoveType::Run);
            }
		}
		break;
	
    case ESocialAction::Trade:
    case ESocialAction::GiveItem:
    case ESocialAction::Comfort:
    case ESocialAction::HandObject:
	default:
		// Fallback: Just emote or log
		FString TargetID = Params.FindRef(NPCActionKeys::Key_TargetID);
		UE_LOG(LogTemp, Warning, TEXT("[BTTask_SocialAction] Unimplemented Social Action: %s (Target: %s) -> Executing generic Emote 'Talk'"), *SubActionStr, *TargetID);
		NPC->ExecuteEmote(TEXT("Talk"));
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_SocialAction::GetStaticDescription() const
{
	if (UEnum* EnumPtr = StaticEnum<ESocialAction>())
	{
		return FString::Printf(TEXT("Execute Social Action: %s"), *EnumPtr->GetValueAsString(SubAction));
	}
	return TEXT("Execute Social Action");
}
