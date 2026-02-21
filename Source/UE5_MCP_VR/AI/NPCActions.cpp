#include "NPCActions.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "SmartNPC.h"
#include "SmartNPCAIController.h"
#include "NPCActionKeys.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ----------------------------------------------------------------------------
// [Base Class] NPCActionBase Implementation
// ----------------------------------------------------------------------------

void UNPCActionBase::ParseBlackboardParams(UBlackboardComponent* BB, TMap<FString, FString>& OutParams, FString& OutSubAction)
{
	if (!BB) return;

	OutSubAction = BB->GetValueAsString(ASmartNPCAIController::Key_SubAction);
	FString ParamsJson = BB->GetValueAsString(ASmartNPCAIController::Key_ActionParameters);

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJson);
	FJsonSerializer::Deserialize(Reader, JsonObj);

	if (JsonObj.IsValid())
	{
		for (auto& Pair : JsonObj->Values)
		{
			OutParams.Add(Pair.Key, Pair.Value->AsString());
		}
	}
}

EBTNodeResult::Type UNPCActionBase::ExecuteCommonFallback(UBehaviorTreeComponent& OwnerComp, const FString& CallerName)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr;
	TMap<FString, FString> Params;
	ParseBlackboardParams(BB, Params, SubActionStr);

	if (SubActionStr.IsEmpty()) return EBTNodeResult::Succeeded;

	const UEnum* EnumPtr = StaticEnum<ECommonAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ECommonAction::%s"), *SubActionStr)));

	if (EnumValue == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CommonFallback] '%s' is not a valid CommonAction (from %s)"), *SubActionStr, *CallerName);
		return EBTNodeResult::Failed;
	}

	ECommonAction Action = static_cast<ECommonAction>(EnumValue);

	switch (Action)
	{
	case ECommonAction::Move:
		{
			FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
			EMoveType MoveType = EMoveType::Walk;
			FString Style = Params.FindRef(TEXT("style"));
			if (Style.IsEmpty()) Style = Params.FindRef(TEXT("speed"));

			if (Style.Equals(TEXT("Run"), ESearchCase::IgnoreCase)) MoveType = EMoveType::Run;
			else if (Style.Equals(TEXT("Sprint"), ESearchCase::IgnoreCase)) MoveType = EMoveType::Sprint;
			else if (Style.Equals(TEXT("Crouch"), ESearchCase::IgnoreCase)) MoveType = EMoveType::Crouch;

			NPC->ExecuteMoveToLocation(Location, MoveType);
		}
		break;

	case ECommonAction::Follow:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			float Distance = 200.0f;
			if (Params.Contains(TEXT("distance"))) Distance = FCString::Atof(*Params[TEXT("distance")]);
			float Speed = NPC->GetStats().Movement.RunSpeed;
			if (Params.Contains(TEXT("speed"))) Speed = FCString::Atof(*Params[TEXT("speed")]);
			NPC->ExecuteKeepDistance(TargetActor, Distance, Speed);
		}
		break;

	case ECommonAction::Dialogue:
		{
			FString Content = Params.FindRef(NPCActionKeys::Key_Text);
			FString Emotion = Params.FindRef(NPCActionKeys::Key_Emotion);
			if (Emotion.IsEmpty()) Emotion = NPCActionKeys::Value_Neutral;
			NPC->ExecuteDialogue(Content, Emotion);
		}
		break;

	case ECommonAction::TurnTo:
		{
			FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
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
		}
		break;

	case ECommonAction::UseItem:
		{
			FString ItemID = Params.FindRef(TEXT("ItemID"));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_Eat, nullptr, ItemID, TEXT(""));
		}
		break;

	case ECommonAction::Stop:
		NPC->StopAllActions();
		break;

	case ECommonAction::Scan:
		{
			FVector RandomPoint = NPC->GetActorLocation() + FMath::VRand() * 100.0f;
			RandomPoint.Z = NPC->GetActorLocation().Z;
			NPC->ExecuteFaceRotate(RandomPoint, 3.0f);
			NPC->ExecuteWait(2.0f);
		}
		break;

	case ECommonAction::Equip:
		NPC->ExecuteInteraction(NPCActionKeys::Interact_Equip, nullptr, Params.FindRef(TEXT("ItemID")), TEXT(""));
		break;

	case ECommonAction::Unequip:
		NPC->ExecuteInteraction(NPCActionKeys::Interact_Unequip, nullptr, Params.FindRef(TEXT("ItemID")), TEXT(""));
		break;

	case ECommonAction::Idle:
	default:
		NPC->ClearPhysicalState();
		break;
	}

	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

// ----------------------------------------------------------------------------
// [Common] UCommonAction Implementation
// ----------------------------------------------------------------------------

UCommonAction::UCommonAction()
{
	NodeName = "Common Action";
	SubAction = ECommonAction::Idle;
}

EBTNodeResult::Type UCommonAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return ExecuteCommonFallback(OwnerComp, TEXT("CommonAction"));
}

FString UCommonAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Common Action from Blackboard"));
}

// ----------------------------------------------------------------------------
// [Combat] UCombatAction Implementation
// ----------------------------------------------------------------------------

UCombatAction::UCombatAction()
{
	NodeName = "Combat Action";
	SubAction = ECombatAction::Attack;
}

EBTNodeResult::Type UCombatAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr;
	TMap<FString, FString> Params;
	ParseBlackboardParams(BB, Params, SubActionStr);

	const UEnum* EnumPtr = StaticEnum<ECombatAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ECombatAction::%s"), *SubActionStr)));

	if (EnumValue == INDEX_NONE)
	{
		return ExecuteCommonFallback(OwnerComp, TEXT("CombatAction"));
	}

	ECombatAction Action = static_cast<ECombatAction>(EnumValue);
	switch (Action)
	{
	case ECombatAction::Attack:
		NPC->ExecutePerformAttack(Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor)), Params.FindRef(TEXT("AttackType")));
		break;
	case ECombatAction::Block: NPC->ExecuteDefend(true); break;
	case ECombatAction::Dodge: NPC->ExecuteDodge(); break;
	default: break;
	}

	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString UCombatAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Combat Action: %s"), *UEnum::GetValueAsString(SubAction));
}

// ----------------------------------------------------------------------------
// [Social] USocialAction Implementation
// ----------------------------------------------------------------------------

USocialAction::USocialAction()
{
	NodeName = "Social Action";
	SubAction = ESocialAction::Emote;
}

EBTNodeResult::Type USocialAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr;
	TMap<FString, FString> Params;
	ParseBlackboardParams(BB, Params, SubActionStr);

	const UEnum* EnumPtr = StaticEnum<ESocialAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*SubActionStr));
	if (EnumValue == INDEX_NONE) EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ESocialAction::%s"), *SubActionStr)));

	if (EnumValue == INDEX_NONE)
	{
		return ExecuteCommonFallback(OwnerComp, TEXT("SocialAction"));
	}

	ESocialAction Action = static_cast<ESocialAction>(EnumValue);
	switch (Action)
	{
	case ESocialAction::Emote:
	{
		FString Gesture = Params.FindRef(TEXT("GestureType"));
		if (Gesture.IsEmpty()) Gesture = Params.FindRef(TEXT("gesture"));
		NPC->ExecuteEmote(Gesture);
	}
	break;
	default: NPC->ExecuteEmote(TEXT("Talk")); break;
	}

	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString USocialAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Social Action: %s"), *UEnum::GetValueAsString(SubAction));
}

// ----------------------------------------------------------------------------
// [Task] UTaskAction Implementation
// ----------------------------------------------------------------------------

UTaskAction::UTaskAction()
{
	NodeName = "Task Action";
	SubAction = ETaskAction::PickUp;
}

EBTNodeResult::Type UTaskAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr;
	TMap<FString, FString> Params;
	ParseBlackboardParams(BB, Params, SubActionStr);

	const UEnum* EnumPtr = StaticEnum<ETaskAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ETaskAction::%s"), *SubActionStr)));

	if (EnumValue == INDEX_NONE)
	{
		return ExecuteCommonFallback(OwnerComp, TEXT("TaskAction"));
	}

	ETaskAction Action = static_cast<ETaskAction>(EnumValue);
	FString TargetID = Params.FindRef(TEXT("ItemID"));
	if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("TargetObject"));
	AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));

	switch (Action)
	{
	case ETaskAction::PickUp: NPC->ExecuteInteraction(NPCActionKeys::Interact_PickUp, TargetActor, TargetID, TEXT("")); break;
	case ETaskAction::Drop: NPC->ExecuteInteraction(NPCActionKeys::Interact_Drop, TargetActor, TargetID, TEXT("")); break;
	case ETaskAction::Craft:
	case ETaskAction::Repair: NPC->ExecuteInteraction(NPCActionKeys::Interact_Repair, TargetActor, TargetID, TEXT("")); break;
	default: break;
	}

	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString UTaskAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Task Action: %s"), *UEnum::GetValueAsString(SubAction));
}

// ----------------------------------------------------------------------------
// [Investigation] UInvestigationAction Implementation
// ----------------------------------------------------------------------------

UInvestigationAction::UInvestigationAction()
{
	NodeName = "Investigation Action";
	SubAction = EInvestigationAction::Investigate;
}

EBTNodeResult::Type UInvestigationAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr;
	TMap<FString, FString> Params;
	ParseBlackboardParams(BB, Params, SubActionStr);

	const UEnum* EnumPtr = StaticEnum<EInvestigationAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("EInvestigationAction::%s"), *SubActionStr)));

	if (EnumValue == INDEX_NONE)
	{
		return ExecuteCommonFallback(OwnerComp, TEXT("InvestigationAction"));
	}

	EInvestigationAction Action = static_cast<EInvestigationAction>(EnumValue);
	switch (Action)
	{
	case EInvestigationAction::Investigate:
	case EInvestigationAction::Scout:
		NPC->ExecuteMoveToLocation(BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation), EMoveType::Walk, 150.0f);
		break;
	default: break;
	}

	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString UInvestigationAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Investigation Action: %s"), *UEnum::GetValueAsString(SubAction));
}

// ----------------------------------------------------------------------------
// [Lifestyle] ULifestyleAction Implementation
// ----------------------------------------------------------------------------

ULifestyleAction::ULifestyleAction()
{
	NodeName = "Lifestyle Action";
	SubAction = ELifestyleAction::Sit;
}

EBTNodeResult::Type ULifestyleAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString SubActionStr;
	TMap<FString, FString> Params;
	ParseBlackboardParams(BB, Params, SubActionStr);

	const UEnum* EnumPtr = StaticEnum<ELifestyleAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ELifestyleAction::%s"), *SubActionStr)));

	if (EnumValue == INDEX_NONE)
	{
		return ExecuteCommonFallback(OwnerComp, TEXT("LifestyleAction"));
	}

	ELifestyleAction Action = static_cast<ELifestyleAction>(EnumValue);
	AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));

	switch (Action)
	{
	case ELifestyleAction::Dance:
	case ELifestyleAction::Sing: NPC->ExecuteEmote(SubActionStr); break;
	case ELifestyleAction::Sit: NPC->ExecuteInteraction(NPCActionKeys::Interact_SitDown, TargetActor, Params.FindRef(TEXT("ChairID")), TEXT("")); break;
	case ELifestyleAction::Sleep: NPC->ExecuteInteraction(NPCActionKeys::Interact_LieDown, TargetActor, Params.FindRef(TEXT("BedID")), TEXT("")); break;
	case ELifestyleAction::Clean: NPC->ExecuteInteraction(NPCActionKeys::Interact_Clean, TargetActor, Params.FindRef(TEXT("Area")), TEXT("")); break;
	case ELifestyleAction::Read: NPC->ExecuteInteraction(NPCActionKeys::Interact_Read, TargetActor, Params.FindRef(TEXT("BookID")), TEXT("")); break;
	case ELifestyleAction::Pray: NPC->ExecuteInteraction(NPCActionKeys::Interact_Pray, TargetActor, Params.FindRef(TEXT("Area")), TEXT("")); break;
	default: break;
	}

	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString ULifestyleAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Lifestyle Action: %s"), *UEnum::GetValueAsString(SubAction));
}

// ----------------------------------------------------------------------------
// [Generic] UPerformInteraction Implementation
// ----------------------------------------------------------------------------

UPerformInteraction::UPerformInteraction()
{
	NodeName = "Perform Interaction";
}

EBTNodeResult::Type UPerformInteraction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController || !AIController->GetPawn()) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FString InteractionKey = BB->GetValueAsString(ASmartNPCAIController::Key_SubAction);
	AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
	FString ParamsJson = BB->GetValueAsString(ASmartNPCAIController::Key_ActionParameters);

	FString TargetID = TEXT("");
	FString ExtraParams = TEXT("");

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
	{
		if (!TargetActor) TargetID = JsonObj->GetStringField(NPCActionKeys::Key_TargetID);
		if (JsonObj->HasField(TEXT("style"))) ExtraParams = JsonObj->GetStringField(TEXT("style"));
		else if (JsonObj->HasField(TEXT("song"))) ExtraParams = JsonObj->GetStringField(TEXT("song"));
	}

	NPC->ExecuteInteraction(InteractionKey, TargetActor, TargetID, ExtraParams);
	return EBTNodeResult::Succeeded;
}

FString UPerformInteraction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Executes Generic Interaction from Blackboard"));
}
