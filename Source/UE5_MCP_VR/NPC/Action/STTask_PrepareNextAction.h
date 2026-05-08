#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionTypes.h"
#include "../Struct/NPCActionTypes.h"
#include "STTask_PrepareNextAction.generated.h"

class ASmartNPCAIController;
class UNPCActionComponent;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

/**
 * ============================================================================
 * FSTTask_PrepareNextAction (StateTree)
 * ============================================================================
 * BTTask_PrepareNextAction의 StateTree 이식.
 * - ActionQueue에 액션이 있으면 Dequeue → ProcessNextAction → Succeeded
 * - 큐가 비었으면 BB의 TargetActor 유무에 따라 Attack/Track 자율 주입 후 Running
 * - InstanceData는 후속 Task가 바인딩할 Output을 노출 (HasAction / SubAction / TargetLocation)
 *
 * 같은 State 안에 ExecuteSmartAction과 함께 두지 말 것 — 별도 State로 분리해야 함.
 */
USTRUCT()
struct FSTTask_PrepareNextActionInstanceData
{
    GENERATED_BODY()

    /** [Output] 큐에서 액션을 꺼냈는지 여부. 다음 State의 Condition으로 바인딩 가능. */
    UPROPERTY(EditAnywhere, Category = "Output")
    bool bHasAction = false;

    /** [Output] 준비된 액션 타입. ExecuteSmartAction이 참조. */
    UPROPERTY(EditAnywhere, Category = "Output")
    EAction SubAction = EAction::Idle;

    /** [Output] target_loc 파라미터에서 파싱된 위치. */
    UPROPERTY(EditAnywhere, Category = "Output")
    FVector TargetLocation = FVector::ZeroVector;

    /** [내부 캐시] 매 틱 Cast<ASmartNPC>를 피하기 위해 EnterState에서 해석. */
    UPROPERTY(Transient)
    TWeakObjectPtr<UNPCActionComponent> CachedActionComp;
};

USTRUCT(meta = (DisplayName = "Prepare Next Action", Category = "NPC|Action"))
struct UE5_MCP_VR_API FSTTask_PrepareNextAction : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTTask_PrepareNextActionInstanceData;

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual bool Link(FStateTreeLinker& Linker) override;
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

    /** AIController는 StateTreeAISchema가 자동 등록한 Context로 접근 가능하지만,
     *  Pawn 캐스팅 + Pawn→ActionComponent 경로가 빈번하므로 ExternalData로 명시적 핸들 보유. */
    TStateTreeExternalDataHandle<ASmartNPCAIController> AIControllerHandle;
};

// ============================================================================
// FSTEvaluator_NPCState
// ============================================================================

USTRUCT()
struct UE5_MCP_VR_API FSTEvaluator_NPCState_InstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Output")
    bool bHasAction = false;

    UPROPERTY(EditAnywhere, Category = "Output")
    ENPCBehaviorMode BehaviorMode = ENPCBehaviorMode::Common;
};

USTRUCT(meta = (DisplayName = "NPC State Evaluator", Category = "NPC State"))
struct UE5_MCP_VR_API FSTEvaluator_NPCState : public FStateTreeEvaluatorCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTEvaluator_NPCState_InstanceData;

    FSTEvaluator_NPCState() = default;

    virtual const UStruct* GetInstanceDataType() const override { return FSTEvaluator_NPCState_InstanceData::StaticStruct(); }

    virtual bool Link(FStateTreeLinker& Linker) override;
    virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
    virtual void TreeStart(FStateTreeExecutionContext& Context) const override;

private:
    TStateTreeExternalDataHandle<ASmartNPCAIController> AIControllerHandle;
};
