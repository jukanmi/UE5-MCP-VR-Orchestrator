#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PrepareNextAction.generated.h"

/**
 * ============================================================================
 * UBTTask_PrepareNextAction
 * ============================================================================
 * ActionQueue에 액션이 있으면 Dequeue → ProcessNextAction → Succeeded.
 * 큐가 비었으면 HasAction=false 세팅 후 Failed 반환 (BT는 Idle 분기로).
 *
 * EQS 전술 쿼리는 Perception 이벤트(OnTargetPerceptionUpdated)에서 독립적으로
 * 트리거된다 (UNPCActionComponent::TryStartTacticalQueryForCombat). 쿼리 완료 시
 * Move 액션이 큐에 자동 enqueue되어 이 Task가 다음 tick에 자연스럽게 처리.
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_PrepareNextAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_PrepareNextAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
