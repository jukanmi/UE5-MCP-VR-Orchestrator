#include "BTTask_PrepareNextAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"
#include "../Struct/NPCActionKeys.h"

UBTTask_PrepareNextAction::UBTTask_PrepareNextAction()
{
	NodeName = TEXT("Prepare Next Action");
}

EBTNodeResult::Type UBTTask_PrepareNextAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ASmartNPCAIController* AICon = Cast<ASmartNPCAIController>(OwnerComp.GetAIOwner());
	if (!AICon) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AICon->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	UNPCActionComponent* ActionComp = NPC->GetActionComponent();
	if (!ActionComp) return EBTNodeResult::Failed;

	// 이미 진행 중인 액션이 있으면 방어 처리
	if (ActionComp->bIsBusy)
	{
		return EBTNodeResult::Failed;
	}

	// 큐에 액션이 있으면 Dequeue → 실행 준비
	if (ActionComp->ProcessNextAction()) return EBTNodeResult::Succeeded;

	// 큐 비어있음 → HasAction 내리고 BT에게 할 일 없음을 알림.
	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		// [주기적 전술 평가] 큐가 비어있고 타겟(적)이 존재하면 전투 중이므로 쿨다운마다 진지 구축을 시도함.
		AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
		if (TargetActor)
		{
			TArray<FVector> EnemyLocs;
			EnemyLocs.Add(TargetActor->GetActorLocation());
			// 쿨다운이 찼다면 전술 쿼리를 발동하고 결과를 기다림
			ActionComp->TryStartTacticalQueryForCombat(EnemyLocs);
		}

		BB->SetValueAsBool(ASmartNPCAIController::Key_HasAction, false);
		BB->SetValueAsEnum(ASmartNPCAIController::Key_SubAction, (uint8)EAction::Idle);
	}
	return EBTNodeResult::Failed;
}

FString UBTTask_PrepareNextAction::GetStaticDescription() const
{
	return TEXT("ActionQueue에서 다음 액션 Dequeue. 큐 비면 HasAction=false로 내림.");
}
