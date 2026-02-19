#include "BTTask_InvestigationAction.h"
#include "BTTask_CommonAction.h"  // CommonFallback 접근용
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

	// 공용 헬퍼로 파라미터 파싱 (중복 코드 제거)
	FString SubActionStr;
	TMap<FString, FString> Params;
	UBTTask_CommonAction::ParseBlackboardParams(BB, Params, SubActionStr);

	// Investigation Enum에서 찾기
	const UEnum* EnumPtr = StaticEnum<EInvestigationAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("EInvestigationAction::%s"), *SubActionStr)));

	// ─────────────────────────────────────────────────────────────────
	// [Fallback] Investigation Enum에 없는 액션 → CommonAction으로 위임
	// 왜: 조사 중에도 "Dialogue"(혼잣말), "TurnTo"(두리번) 같은 Common 필요
	// ─────────────────────────────────────────────────────────────────
	if (EnumValue == INDEX_NONE)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[InvestigationAction] '%s' not in EInvestigationAction, delegating to CommonFallback"),
			*SubActionStr);
		return UBTTask_CommonAction::ExecuteCommonFallback(OwnerComp, TEXT("InvestigationAction"));
	}

	EInvestigationAction Action = static_cast<EInvestigationAction>(EnumValue);
	SubAction = Action;

	// Investigation 전용 로직
	switch (Action)
	{
	case EInvestigationAction::Investigate:
	case EInvestigationAction::Scout:
		{
			// 조사 지점으로 천천히 이동
			FVector Location = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
			NPC->ExecuteMoveToLocation(Location, EMoveType::Walk, 150.0f);
		}
		break;
	
	default:
		{
			// Track → ExecuteGenericAction
			FString TargetID = Params.FindRef(TEXT("TargetID"));
			if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("TargetTrace"));
			NPC->ExecuteGenericAction(SubActionStr, TargetID);
		}
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_InvestigationAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Investigation Action: %s"), *UEnum::GetValueAsString(SubAction));
}
