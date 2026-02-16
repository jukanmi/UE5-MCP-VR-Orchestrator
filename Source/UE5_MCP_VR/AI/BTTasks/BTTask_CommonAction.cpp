#include "BTTask_CommonAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "../NPCActionKeys.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_CommonAction::UBTTask_CommonAction()
{
	NodeName = "Common Action";
	SubAction = ECommonAction::Idle; //BasicAction
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
		
		ASmartNPC::EMoveType MoveType = ASmartNPC::EMoveType::Walk;
		FString Style = Params.FindRef(TEXT("style"));
		if (Style.IsEmpty()) Style = Params.FindRef(TEXT("speed")); // Legacy fallback

		if (Style.Equals(TEXT("Run"), ESearchCase::IgnoreCase)) MoveType = ASmartNPC::EMoveType::Run;
		else if (Style.Equals(TEXT("Sprint"), ESearchCase::IgnoreCase)) MoveType = ASmartNPC::EMoveType::Sprint;
		else if (Style.Equals(TEXT("Crouch"), ESearchCase::IgnoreCase)) MoveType = ASmartNPC::EMoveType::Crouch;

		NPC->ExecuteMoveToLocation(Location, MoveType);
	}
	break;
	
	case ECommonAction::Follow:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			float Distance = 200.0f; // Default
			if (Params.Contains(TEXT("distance"))) Distance = FCString::Atof(*Params[TEXT("distance")]);
			
			// Speed mapping? Default to Run for follow
			float Speed = NPC->CurrentStats.Movement.RunSpeed;
            if (Params.Contains(TEXT("speed"))) Speed = FCString::Atof(*Params[TEXT("speed")]);

			NPC->ExecuteKeepDistance(TargetActor, Distance, Speed);
		}
		break;

	case ECommonAction::Dialogue:
		{
			FString Content = Params.FindRef(NPCActionKeys::Key_Text);
			if (Content.IsEmpty()) Content = Params.FindRef(NPCActionKeys::Key_Content); // Fallback
			if (Content.IsEmpty()) Content = Params.FindRef(TEXT("Info")); // Legacy

			FString Emotion = Params.FindRef(NPCActionKeys::Key_Emotion);
			if (Emotion.IsEmpty()) Emotion = NPCActionKeys::Value_Neutral;

			NPC->ExecuteDialogue(Content, Emotion);
		}
		break;

	case ECommonAction::UseItem:
		{
			FString ItemID = Params.FindRef(TEXT("ItemID"));
            if (ItemID.IsEmpty()) ItemID = Params.FindRef(TEXT("id"));
			// UseItem function not in SmartNPC Action list explicitly, assuming Emote or logging?
            // Actually, SmartNPC.h declared ExecuteActionBatch but not specific UseItem function in snippet 167.
            // Wait, snippet 167 has: ExecuteEmote, ExecuteHandSignal, etc.
            // But Action Keys define Action_PickUp.
            // UseItem was in BTTask_CommonActions.h enum.
            // SmartNPC doesn't have ExecuteUseItem exposed in snippet 167.
            // Log for now.
			UE_LOG(LogTemp, Warning, TEXT("UseItem Action not fully implemented in SmartNPC. Agent: %s, Item: %s"), *NPC->AgentID, *ItemID);
		}
		break;

	case ECommonAction::TurnTo:
		{
			FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
            // Or prioritize TargetActor?
            if (UObject* TargetObj = BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor))
            {
                 if (AActor* Act = Cast<AActor>(TargetObj)) Location = Act->GetActorLocation();
            }
			NPC->ExecuteFaceRotate(Location);
		}
		break;
	
    case ECommonAction::Wait:
        {
            float Duration = 2.0f;
            if (Params.Contains(TEXT("duration"))) Duration = FCString::Atof(*Params[TEXT("duration")]);
            NPC->ExecuteWait(Duration);
            // Note: The Wait functionality (delay) usually handled by BT Task "Wait".
            // This just triggers animation/log.
        }
        break;

	case ECommonAction::Idle:
	default: 
		// Just clear state
		NPC->ClearPhysicalState();
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_CommonAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Common Action from Blackboard"));
}
