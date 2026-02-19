#include "BTTask_TaskAction.h"
#include "BTTask_CommonAction.h"  // CommonFallback 접근용
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

	// 공용 헬퍼로 파라미터 파싱 (중복 코드 제거)
	FString SubActionStr;
	TMap<FString, FString> Params;
	UBTTask_CommonAction::ParseBlackboardParams(BB, Params, SubActionStr);

	// Task Enum에서 찾기
	const UEnum* EnumPtr = StaticEnum<ETaskAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ETaskAction::%s"), *SubActionStr)));

	// ─────────────────────────────────────────────────────────────────
	// [Fallback] Task Enum에 없는 액션 → CommonAction으로 위임
	// 왜: 물건 줍기(Task) 후 "이동"이나 "말하기" 같은 Common 액션 필요
	// ─────────────────────────────────────────────────────────────────
	if (EnumValue == INDEX_NONE)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[TaskAction] '%s' not in ETaskAction, delegating to CommonFallback"),
			*SubActionStr);
		return UBTTask_CommonAction::ExecuteCommonFallback(OwnerComp, TEXT("TaskAction"));
	}

	ETaskAction Action = static_cast<ETaskAction>(EnumValue);
	SubAction = Action;

	// Task 전용 로직: 현재는 모든 액션이 GenericAction으로 처리
	FString TargetID = Params.FindRef(TEXT("ItemID"));
	if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("TargetObject"));
	if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("RecipeID"));
	NPC->ExecuteGenericAction(SubActionStr, TargetID);

	return EBTNodeResult::Succeeded;
}

FString UBTTask_TaskAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Task Action: %s"), *UEnum::GetValueAsString(SubAction));
}
