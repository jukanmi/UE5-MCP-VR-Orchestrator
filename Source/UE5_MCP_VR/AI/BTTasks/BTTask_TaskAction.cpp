#include "BTTask_TaskAction.h"
#include "BTTask_CommonAction.h"  // CommonFallback 접근용
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "../NPCActionKeys.h"

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

	// 빈 SubAction 가드: 대기 중인 액션이 없으면 조용히 성공 반환
	if (SubActionStr.IsEmpty()) return EBTNodeResult::Succeeded;

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

	// Task 전용 로직
	FString TargetID = Params.FindRef(TEXT("ItemID"));
	if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("TargetObject"));
	if (TargetID.IsEmpty()) TargetID = Params.FindRef(TEXT("RecipeID"));

	switch (Action)
	{
	case ETaskAction::PickUp:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_PickUp, TargetActor, TargetID, TEXT(""));
		}
		break;

	case ETaskAction::Drop:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_Drop, TargetActor, TargetID, TEXT(""));
		}
		break;

	case ETaskAction::Craft:
	case ETaskAction::Repair:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_Repair, TargetActor, TargetID, TEXT(""));
		}
		break;

	default:
		// Unknown Task Action
		break;
	}

	// 액션 완료 → 큐 진행
	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();

	return EBTNodeResult::Succeeded;
}

FString UBTTask_TaskAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Task Action: %s"), *UEnum::GetValueAsString(SubAction));
}
