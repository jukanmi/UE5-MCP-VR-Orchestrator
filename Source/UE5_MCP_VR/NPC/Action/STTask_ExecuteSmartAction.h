#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeExecutionTypes.h"
#include "STTask_ExecuteSmartAction.generated.h"

class ASmartNPCAIController;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

/**
 * ============================================================================
 * FSTTask_ExecuteSmartAction (StateTree)
 * ============================================================================
 * BTTask_ExecuteSmartAction의 StateTree 이식.
 * - 준비된 CurrentAction을 NPCActionComponent::ExecuteInteraction에 단일 위임
 * - 동기 실행 모델 유지 (호출 직후 OnActionCompleted → Succeeded)
 * - TargetActor는 Blackboard에서 직접 조회 (Perception 콜백이 BB를 갱신하므로)
 *
 * 같은 State 안에 PrepareNextAction과 함께 두지 말 것 — 별도 State로 분리.
 */
USTRUCT()
struct FSTTask_ExecuteSmartActionInstanceData
{
    GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Execute Smart Action", Category = "NPC|Action"))
struct UE5_MCP_VR_API FSTTask_ExecuteSmartAction : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTTask_ExecuteSmartActionInstanceData;

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual bool Link(FStateTreeLinker& Linker) override;
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

    TStateTreeExternalDataHandle<ASmartNPCAIController> AIControllerHandle;
};
