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
	SubAction = ECommonAction::Idle;
}


// ─────────────────────────────────────────────────────────────────────────────
// 공용 헬퍼: Blackboard → TMap<FString, FString> 파싱
// 왜 분리했는가: 6개 BTTask .cpp에 동일 코드가 복붙되어 있어서,
// JSON 형식이 바뀌면 6곳을 모두 수정해야 했음 → 1곳에서 관리.
// ─────────────────────────────────────────────────────────────────────────────
void UBTTask_CommonAction::ParseBlackboardParams(
	UBlackboardComponent* BB,
	TMap<FString, FString>& OutParams,
	FString& OutSubAction)
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


// ─────────────────────────────────────────────────────────────────────────────
// 핵심: CommonAction 로직 실행 (static 버전)
// 왜 static인가: 다른 모드별 BTTask에서 인스턴스 없이 호출할 수 있도록.
// Combat 중 "Move" 명령이 들어오면 이 함수로 위임됨.
// ─────────────────────────────────────────────────────────────────────────────
EBTNodeResult::Type UBTTask_CommonAction::ExecuteCommonFallback(
	UBehaviorTreeComponent& OwnerComp,
	const FString& CallerName)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	// 1. Blackboard에서 파라미터 파싱
	FString SubActionStr;
	TMap<FString, FString> Params;
	ParseBlackboardParams(BB, Params, SubActionStr);

	// 빈 SubAction 가드: BT가 매 프레임 Tick하지만 대기 중인 액션이 없는 경우
	// 빈 문자열로 Enum 매칭을 시도하면 항상 실패 → 경고 로그 무한 반복
	// "할 일이 없다"는 에러가 아니므로 조용히 성공 반환
	if (SubActionStr.IsEmpty())
	{
		return EBTNodeResult::Succeeded;
	}

	// 2. SubAction 문자열 → ECommonAction Enum 변환 (전체 이름으로 통일)
	// 왜 한 번만 시도하는가: UE5 UENUM은 "EnumName::Value" 형식으로만 매칭됨.
	// 짧은 이름("Move")으로는 항상 INDEX_NONE → 불필요한 시도 제거.
	const UEnum* EnumPtr = StaticEnum<ECommonAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ECommonAction::%s"), *SubActionStr)));

	// 매칭 실패 시 로그 출력 후 실패 반환
	if (EnumValue == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CommonFallback] '%s' is not a valid CommonAction (called from %s)"),
			*SubActionStr, *CallerName);
		return EBTNodeResult::Failed;
	}

	ECommonAction Action = static_cast<ECommonAction>(EnumValue);

	UE_LOG(LogTemp, Log,
		TEXT("[CommonFallback] Executing '%s' as Common action (delegated from %s)"),
		*SubActionStr, *CallerName);

	// 3. CommonAction switch 분기 실행
	switch (Action)
	{
	case ECommonAction::Move:
	{
		FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);

		EMoveType MoveType = EMoveType::Walk;
		FString Style = Params.FindRef(TEXT("style"));
		if (Style.IsEmpty()) Style = Params.FindRef(TEXT("speed")); // Legacy fallback

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

		float Speed = NPC->CurrentStats.Movement.RunSpeed;
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
		if (ItemID.IsEmpty()) ItemID = Params.FindRef(TEXT("id"));
		UE_LOG(LogTemp, Warning,
			TEXT("UseItem Action not fully implemented. Agent: %s, Item: %s"),
			*NPC->AgentID, *ItemID);
	}
	break;

	case ECommonAction::Stop:
	{
		NPC->StopAllActions();
	}
	break;

	case ECommonAction::Scan:
	{
		// 주변 두리번거리기: 현재는 GenericAction으로 처리
		NPC->ExecuteGenericAction(TEXT("Scan"), TEXT(""));
	}
	break;

	case ECommonAction::Equip:
	case ECommonAction::Unequip:
	{
		FString ItemID = Params.FindRef(TEXT("ItemID"));
		if (ItemID.IsEmpty()) ItemID = Params.FindRef(TEXT("id"));
		NPC->ExecuteGenericAction(SubActionStr, ItemID);
	}
	break;

	case ECommonAction::Idle:
	default:
		NPC->ClearPhysicalState();
		break;
	}

	// 액션 실행 완료 → NPC에게 통지하여 ActionQueue의 다음 액션 진행
	// 왜: ProcessNextAction에서 bIsBusy=true로 설정 → BTTask 완료 시
	// OnActionCompleted을 호출하지 않으면 큐가 영원히 멈춤
	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();

	return EBTNodeResult::Succeeded;
}


// ─────────────────────────────────────────────────────────────────────────────
// ExecuteTask: BT 노드에서 직접 실행될 때 호출됨
// 내부적으로 ExecuteCommonFallback을 재사용하여 코드 중복 제거
// ─────────────────────────────────────────────────────────────────────────────
EBTNodeResult::Type UBTTask_CommonAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return ExecuteCommonFallback(OwnerComp, TEXT("CommonAction"));
}

FString UBTTask_CommonAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Common Action from Blackboard"));
}
