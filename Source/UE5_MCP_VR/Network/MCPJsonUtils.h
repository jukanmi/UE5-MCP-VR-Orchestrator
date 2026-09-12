#pragma once

#include "NPC/Struct/NPCActionTypes.h"
#include "Core/Types/GameStateData.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SharedPointer.h"
#include "MCPJsonUtils.generated.h"

class FJsonObject;

/**
 * Utility class for parsing JSON from Cognitive Engine
 */
UCLASS()
class UE5_MCP_VR_API UMCPJsonUtils : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Python 백엔드로부터 수신된(이미 deserialize 된) ActionBatch JSON 오브젝트를 FModeActionRequest 로 역직렬화. */
    static bool ParseModeActionRequestFromObject(const TSharedPtr<FJsonObject>& Root, FModeActionRequest& OutRequest);

    // [의도(Why)] 인지(Perception) 이벤트들을 배칭하여 JSON 문자열로 변환합니다.
    // ReportType 이 비어있지 않으면 루트에 report_type 필드를 추가 — Python 이 보고 성격을 구분
    // (예: "combat_victory" 는 danger 게이트 우회). 비우면 기존 perception 보고와 동일(하위호환).
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    /** @param ReflexAction 척수반사가 방금 실행한 EAction 이름. 비면 필드 생략(하위호환) — report_type 과 동일 패턴. */
    static FString SerializePerceptionReport(const FString& AgentID, const TArray<FPerceptionData>& PerceptionEvents, const FString& ReportType = TEXT(""), const FString& ReflexAction = TEXT(""));

    /** location_decision_result 메시지 파싱.
     *  { "type": "location_decision_result", "payload": { "agent_id": "...", "chosen_id": "...", "request_gen": N } }
     *  request_gen 미포함 시 OutRequestGen=0 — stale 검사를 건너뜀(레거시 호환).
     *  @return true이면 OutAgentId / OutChosenId에 값이 채워짐 */
    static bool ParseLocationDecisionResultFromObject(
        const TSharedPtr<FJsonObject>& Root, FString& OutAgentId, FString& OutChosenId, FString& OutReason, int32& OutRequestGen);

    /** state_update 응답에서 relations 파싱.
     *  { "status": "cached", "agent_id": "...", "relations": [{"target_id":"...", "affinity_score":N, ...}] }
     *  @return true이면 OutAgentId와 OutRelations(TargetID→Score)에 값이 채워짐 */
    static bool ParseAffinityUpdateFromObject(
        const TSharedPtr<FJsonObject>& Root, FString& OutAgentId, TMap<FString, int32>& OutRelations);

    /** debug_prompt 메시지 파싱 (Python 디버그 대시보드 → UE5).
     *  성공 시 OutNpcId/OutPlayerId/OutText 채움.
     *  type 이 "debug_prompt" 가 아니면 false 반환 (조용히 패스). */
    static bool ParseDebugPromptFromObject(
        const TSharedPtr<FJsonObject>& Root,
        FString& OutNpcId,
        FString& OutPlayerId,
        FString& OutText);
};
