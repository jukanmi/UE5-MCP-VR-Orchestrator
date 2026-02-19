#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_CommonActions.h"
#include "BTTask_CommonAction.generated.h"

/**
 * ============================================================================
 * UBTTask_CommonAction - 공용/기본 액션 BT Task
 * ============================================================================
 *
 * [핵심 역할]
 * Move, Dialogue, Wait 등 모든 모드에서 공통으로 쓰이는 기본 행동 처리.
 *
 * [설계 의도 - Common Fallback 패턴]
 * 다른 모드별 BTTask(Combat, Social 등)에서 자기 Enum에 없는 action_type이
 * 들어오면, ExecuteCommonFallback()을 호출하여 Common 로직으로 위임한다.
 *
 * 예시: Combat 모드에서 "Move" 액션 → ECombatAction에 없음
 *       → UBTTask_CommonAction::ExecuteCommonFallback() 호출
 *       → ECommonAction::Move 로직 실행 ✅
 *
 * [사용되는 곳]
 * - BTTask_CommonAction::ExecuteTask() (직접 실행)
 * - BTTask_CombatAction::ExecuteTask() (폴백 위임)
 * - BTTask_SocialAction::ExecuteTask() (폴백 위임)
 * - BTTask_TaskAction::ExecuteTask() (폴백 위임)
 * - BTTask_InvestigationAction::ExecuteTask() (폴백 위임)
 * - BTTask_LifestyleAction::ExecuteTask() (폴백 위임)
 */
UCLASS()
class UE5_MCP_VR_API UBTTask_CommonAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CommonAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	// ─────────────────────────────────────────────────────────────────────
	// 공용 유틸리티: 다른 모드별 BTTask에서도 사용
	// ─────────────────────────────────────────────────────────────────────

	/**
	 * 다른 모드 BTTask에서 자기 Enum에 없는 action_type이 들어왔을 때 호출.
	 * CommonAction의 switch 로직을 그대로 실행하되, 인스턴스 없이 static으로 동작.
	 *
	 * @param OwnerComp  BehaviorTree 컴포넌트 (Blackboard 접근용)
	 * @param CallerName 호출한 BTTask 이름 (로깅용, 예: "CombatAction")
	 * @return EBTNodeResult::Succeeded or Failed
	 */
	static EBTNodeResult::Type ExecuteCommonFallback(
		UBehaviorTreeComponent& OwnerComp,
		const FString& CallerName = TEXT("Unknown"));

	/**
	 * Blackboard의 ActionParameters JSON을 TMap으로 파싱하는 공용 헬퍼.
	 * 왜 별도 함수로 분리했는가: 6개 BTTask .cpp에서 동일한 파싱 코드가 반복되어,
	 * JSON 형식 변경 시 6곳을 동시에 수정해야 하는 리스크를 제거.
	 *
	 * @param BB           Blackboard 컴포넌트
	 * @param OutParams    파싱된 Key-Value 파라미터 맵 (출력)
	 * @param OutSubAction SubAction 문자열 (출력)
	 */
	static void ParseBlackboardParams(
		UBlackboardComponent* BB,
		TMap<FString, FString>& OutParams,
		FString& OutSubAction);

protected:
	UPROPERTY(EditAnywhere, Category = "Action")
	ECommonAction SubAction;
};
