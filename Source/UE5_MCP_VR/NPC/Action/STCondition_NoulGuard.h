#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeExecutionContext.h"
#include "STCondition_NoulGuard.generated.h"

/**
 * FSTCondition_NoulGuard — jevlike Noul(유해/탈옥 확률) 가드레일.
 * 상태 진입 전 사전조건으로 두고 HarmfulProbability 를 FSTEvaluator_JevTactics 출력에 바인딩한다.
 * 유해 확률이 임계값(기본 0.85)을 넘으면 해당 State 진입을 차단한다.
 */
USTRUCT()
struct UE5_MCP_VR_API FSTCondition_NoulGuardInstanceData
{
    GENERATED_BODY()

    /** [Input] Evaluator 의 HarmfulProbability 바인딩. 미바인딩(0.0)이면 항상 통과. */
    UPROPERTY(EditAnywhere, Category = "Input")
    float HarmfulProbability = 0.0f;

    /** 이 값을 초과하면 차단. */
    UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Threshold = 0.85f;
};

USTRUCT(meta = (DisplayName = "Jev Noul Guard", Category = "NPC|Jev"))
struct UE5_MCP_VR_API FSTCondition_NoulGuard : public FStateTreeConditionCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSTCondition_NoulGuardInstanceData;

    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual bool TestCondition(FStateTreeExecutionContext& Context) const override
    {
        const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
        // 유해 확률이 임계값 이하일 때만 통과(True).
        return InstanceData.HarmfulProbability <= InstanceData.Threshold;
    }
};
