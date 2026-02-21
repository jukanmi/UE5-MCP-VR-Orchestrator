#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTNode.h"
#include "NPCActionTypes.h"
#include "NPCActions.generated.h"

/**
 * ============================================================================
 * [Base Class] UNPCActionBase
 * ============================================================================
 * 모든 NPC 액션 관련 BT의 공통 기능을 담은 기저 클래스입니다.
 * 중복된 파라미터 파싱 및 공용 폴백 로직을 중앙 관리합니다.
 */
UCLASS(Abstract)
class UE5_MCP_VR_API UNPCActionBase : public UBTNode
{
	GENERATED_BODY()

public:
	/** Blackboard에서 SubAction 이름과 JSON 파라미터를 TMap으로 파싱합니다. */
	static void ParseBlackboardParams(class UBlackboardComponent* BB, TMap<FString, FString>& OutParams, FString& OutSubAction);

	/** 현재 모드 전용 Enum에 없는 액션을 CommonAction으로 위임 처리합니다. */
	static EBTNodeResult::Type ExecuteCommonFallback(UBehaviorTreeComponent& OwnerComp, const FString& CallerName);
};

/**
 * ============================================================================
 * [Common] UCommonAction
 * ============================================================================
 */
UCLASS()
class UE5_MCP_VR_API UCommonAction : public UNPCActionBase
{
	GENERATED_BODY()

public:
	UCommonAction();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, Category = "Action")
	ECommonAction SubAction;
};

/**
 * ============================================================================
 * [Combat] UCombatAction
 * ============================================================================
 */
UCLASS()
class UE5_MCP_VR_API UCombatAction : public UNPCActionBase
{
	GENERATED_BODY()

public:
	UCombatAction();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, Category = "Action")
	ECombatAction SubAction;
};

/**
 * ============================================================================
 * [Social] USocialAction
 * ============================================================================
 */
UCLASS()
class UE5_MCP_VR_API USocialAction : public UNPCActionBase
{
	GENERATED_BODY()

public:
	USocialAction();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, Category = "Action")
	ESocialAction SubAction;
};

/**
 * ============================================================================
 * [Task] UTaskAction
 * ============================================================================
 */
UCLASS()
class UE5_MCP_VR_API UTaskAction : public UNPCActionBase
{
	GENERATED_BODY()

public:
	UTaskAction();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, Category = "Action")
	ETaskAction SubAction;
};

/**
 * ============================================================================
 * [Investigation] UInvestigationAction
 * ============================================================================
 */
UCLASS()
class UE5_MCP_VR_API UInvestigationAction : public UNPCActionBase
{
	GENERATED_BODY()

public:
	UInvestigationAction();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, Category = "Action")
	EInvestigationAction SubAction;
};

/**
 * ============================================================================
 * [Lifestyle] ULifestyleAction
 * ============================================================================
 */
UCLASS()
class UE5_MCP_VR_API ULifestyleAction : public UNPCActionBase
{
	GENERATED_BODY()

public:
	ULifestyleAction();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, Category = "Action")
	ELifestyleAction SubAction;
};