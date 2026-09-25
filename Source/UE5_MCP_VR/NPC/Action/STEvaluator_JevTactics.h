#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionTypes.h"
#include "NPC/Action/SmartNPCAIController.h"
#include "STEvaluator_JevTactics.generated.h"

struct FStateTreeExecutionContext;

/**
 * FSTEvaluator_JevTactics — 컨트롤러의 jevlike 결정 캐시를 0ms 로 복사해 StateTree 에 노출한다.
 * 네트워크 요청·비동기 대기는 여기서 하지 않는다(수명주기는 컨트롤러 소유, UAF 회피).
 * TTL(2.0s) 만료·미수신은 컨트롤러 GetFreshJevDecision() 이 중립(Default/0.0)으로 돌려준다.
 * Output 을 서브 상태 전이 조건(Enum Compare)·FSTCondition_NoulGuard 입력에 바인딩해 쓴다.
 */
USTRUCT()
struct UE5_MCP_VR_API FSTEvaluator_JevTacticsInstanceData
{
    GENERATED_BODY()

    /** [Output] 전술 태도. Alert/Common 내부 서브 상태 전이 조건에 바인딩. */
    UPROPERTY(EditAnywhere, Category = "Output")
    EJevTacticalStance TacticalStance = EJevTacticalStance::Default;

    /** [Output] 태도 신뢰도(0~1). 만료 시 0. */
    UPROPERTY(EditAnywhere, Category = "Output")
    float Confidence = 0.0f;

    /** [Output] Noul 유해 확률(0~1). NoulGuard 조건 입력. */
    UPROPERTY(EditAnywhere, Category = "Output")
    float HarmfulProbability = 0.0f;
};

USTRUCT(meta = (DisplayName = "Jev Tactics Evaluator", Category = "NPC|Jev"))
struct UE5_MCP_VR_API FSTEvaluator_JevTactics : public FStateTreeEvaluatorCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTEvaluator_JevTacticsInstanceData;

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
    virtual bool Link(FStateTreeLinker& Linker) override;
    virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

    TStateTreeExternalDataHandle<ASmartNPCAIController> AIControllerHandle;
};
