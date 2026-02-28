#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "../Struct/NPCActionTypes.h"
#include "BTTask_ExecuteSmartAction.generated.h"

/**
 * ============================================================================
 * UBTTask_ExecuteSmartAction
 * ============================================================================
 * 단일화된 스마트 NPC 액션 실행 마스터 노드입니다.
 * 하위 분류(Combat, Lifestyle 등) 없이, 파이썬 서버가 내려준 구체적인 EAction 플래그와 파라미터를
 * 통째로 읽어 NPCActionComponent에 안전하게 위임(Delegate)합니다.
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_ExecuteSmartAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ExecuteSmartAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
