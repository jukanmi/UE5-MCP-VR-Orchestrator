#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_NotifyActionCompleted.generated.h"

/**
 * 큐 액션 수행(이동, 대기 등 언리얼 내장 노드 활용 시)이 완료되었음을 
 * NPCActionComponent에 알려주는 BTTask입니다.
 * 호출 시 NPCActionComponent의 OnActionCompleted()를 실행합니다.
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_NotifyActionCompleted : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_NotifyActionCompleted();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
