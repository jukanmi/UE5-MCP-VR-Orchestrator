#include "BTTask_CombatAction.h"
#include "BTTask_CommonAction.h"  // CommonFallback 접근용
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "../NPCActionKeys.h"
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

	// 공용 헬퍼로 파라미터 파싱 (중복 코드 제거)
	FString SubActionStr;
	TMap<FString, FString> Params;
	UBTTask_CommonAction::ParseBlackboardParams(BB, Params, SubActionStr);

	// Combat Enum에서 찾기
	const UEnum* EnumPtr = StaticEnum<ECombatAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ECombatAction::%s"), *SubActionStr)));

	// ─────────────────────────────────────────────────────────────────
	// [Fallback] Combat Enum에 없는 액션 → CommonAction으로 위임
	// 왜: NPC가 전투 중에도 "Move", "Dialogue" 같은 기본 행동이 필요함.
	// 예시: 적에게 달려가며 외침 → Move + Dialogue (둘 다 Common)
	// ─────────────────────────────────────────────────────────────────
	if (EnumValue == INDEX_NONE)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CombatAction] '%s' not in ECombatAction, delegating to CommonFallback"),
			*SubActionStr);
		return UBTTask_CommonAction::ExecuteCommonFallback(OwnerComp, TEXT("CombatAction"));
	}

	ECombatAction Action = static_cast<ECombatAction>(EnumValue);
	SubAction = Action;

	// Combat 전용 로직
	switch (Action)
	{
	case ECombatAction::Attack:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			FString AttackType = Params.FindRef(TEXT("AttackType"));
			if (AttackType.IsEmpty()) AttackType = TEXT("Default");
			NPC->ExecutePerformAttack(TargetActor, AttackType);
		}
		break;
	
	default:
		{
			// Block, Dodge, Flee, SignalAllies → ExecuteGenericAction
			FString TargetID = Params.FindRef(TEXT("TargetID"));
			NPC->ExecuteGenericAction(SubActionStr, TargetID);
		}
		break;
	}

	// 액션 완료 → 큐 진행
	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();

	return EBTNodeResult::Succeeded;
}

FString UBTTask_CombatAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Combat Action: %s"), *UEnum::GetValueAsString(SubAction));
}
