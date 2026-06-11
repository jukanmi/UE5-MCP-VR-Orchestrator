#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeExecutionTypes.h"
#include "STTask_ExecuteSmartAction.generated.h"

class ASmartNPCAIController;
class UNPCActionComponent;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

/**
 * ============================================================================
 * FSTTask_ExecuteSmartAction (StateTree)
 * ============================================================================
 * BTTask_ExecuteSmartAction의 StateTree 이식.
 * - 준비된 CurrentAction을 NPCActionComponent::ExecuteInteraction에 단일 위임
 * - 비동기 실행 모델: EnterState에서 실행만 발행하고 Running 반환. Tick에서 bIsBusy를
 *   폴링해 false가 되면(이동 도착/몽타주 종료/즉시형 완료) Succeeded로 종료.
 * - TargetActor는 Blackboard에서 직접 조회 (Perception 콜백이 BB를 갱신하므로)
 *
 * 같은 State 안에 PrepareNextAction과 함께 두지 말 것 — 별도 State로 분리.
 */
USTRUCT()
struct FSTTask_ExecuteSmartActionInstanceData
{
    GENERATED_BODY()

    /** [내부 캐시] 매 틱 Cast<ASmartNPC>를 피하기 위해 EnterState에서 해석. */
    UPROPERTY(Transient)
    TWeakObjectPtr<UNPCActionComponent> CachedActionComp;
};

USTRUCT(meta = (DisplayName = "Execute Smart Action", Category = "NPC|Action"))
struct UE5_MCP_VR_API FSTTask_ExecuteSmartAction : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTTask_ExecuteSmartActionInstanceData;

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual bool Link(FStateTreeLinker& Linker) override;
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

    TStateTreeExternalDataHandle<ASmartNPCAIController> AIControllerHandle;
};
