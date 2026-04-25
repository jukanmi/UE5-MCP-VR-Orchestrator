#include "BTTask_NotifyActionCompleted.h"

UBTTask_NotifyActionCompleted::UBTTask_NotifyActionCompleted()
{
	NodeName = TEXT("Notify Action Completed");
}

EBTNodeResult::Type UBTTask_NotifyActionCompleted::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 완료 신호는 BTTask_ExecuteSmartAction에서 동기적으로 처리됨 — 레거시 BT 그래프 호환용 no-op
	return EBTNodeResult::Succeeded;
}

FString UBTTask_NotifyActionCompleted::GetStaticDescription() const
{
	return TEXT("[레거시 no-op] 완료 신호는 BTTask_ExecuteSmartAction에서 처리됩니다.");
}
