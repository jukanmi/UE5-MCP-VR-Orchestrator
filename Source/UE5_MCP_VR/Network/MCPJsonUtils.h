#pragma once

#include "../NPC/Struct/NPCActionTypes.h"
#include "../Core/GameStateData.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPJsonUtils.generated.h"

/**
 * Utility class for parsing JSON from Cognitive Engine
 */
UCLASS()
class UE5_MCP_VR_API UMCPJsonUtils : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Python 백엔드로부터 수신된 ActionBatch JSON을 FModeActionRequest 구조체로 역직렬화합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static bool ParseModeActionRequest(FString Json, FModeActionRequest& OutRequest);


    // [의도(Why)] 인지(Perception) 이벤트들을 배칭하여 JSON 문자열로 변환합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static FString SerializePerceptionReport(const FString& AgentID, const TArray<FPerceptionData>& PerceptionEvents);

    /** location_decision_result 메시지 파싱.
     *  { "type": "location_decision_result", "payload": { "agent_id": "...", "chosen_id": "..." } }
     *  @return true이면 OutAgentId / OutChosenId에 값이 채워짐 */
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static bool ParseLocationDecisionResult(
        const FString& Json, FString& OutAgentId, FString& OutChosenId, FString& OutReason);

    /** state_update 응답에서 relations 파싱.
     *  { "status": "cached", "agent_id": "...", "relations": [{"target_id":"...", "affinity_score":N, ...}] }
     *  @return true이면 OutAgentId와 OutRelations(TargetID→Score)에 값이 채워짐 */
    static bool ParseAffinityUpdate(
        const FString& Json, FString& OutAgentId, TMap<FString, int32>& OutRelations);
};
