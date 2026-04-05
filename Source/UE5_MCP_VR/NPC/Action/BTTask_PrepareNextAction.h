#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PrepareNextAction.generated.h"

/**
 * ============================================================================
 * UBTTask_PrepareNextAction
 * ============================================================================
 * [경로 1] ActionQueue에 액션이 있으면 → Dequeue → Succeeded (기존 동작)
 * [경로 2] 큐가 비어 있고 적이 인지된 상태이면 →
 *          EQS AllMatching 실행 → 로컬 스코어링 → LLM에 후보 전송 → InProgress
 *          LLM 응답(chosen_id) 수신 → TacticalQueryState=ResultReady → Succeeded
 *
 * WHY: 전술 이동의 최종 판단만 LLM에 위임함으로써 LLM 호출 빈도와
 *      페이로드 크기를 모두 줄이고 응답 지연을 단축한다.
 */

/** BTTask NodeMemory: 전술 쿼리 실행 중 여부를 저장 */
struct FBTPrepareNodeMemory
{
    bool bRunningTacticalQuery = false;
};

UCLASS()
class UE5_MCP_VR_API UBTTask_PrepareNextAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_PrepareNextAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTPrepareNodeMemory); }
	virtual FString GetStaticDescription() const override;
};
