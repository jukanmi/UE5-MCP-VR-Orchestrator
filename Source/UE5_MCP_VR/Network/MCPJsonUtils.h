#pragma once

#include "../NPC/Struct/NPCActionTypes.h"
#include "../Core/GameStateData.h"
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
    // Python 백엔드로부터 수신된 ActionBatch JSON을 FModeActionRequest 구조체로 역직렬화합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static bool ParseModeActionRequest(FString Json, FModeActionRequest& OutRequest);

    /** 이미 deserialize된 JSON 오브젝트로부터 ModeActionRequest 추출 (재파싱 방지) */
    static bool ParseModeActionRequestFromObject(const TSharedPtr<FJsonObject>& Root, FModeActionRequest& OutRequest);


    // [의도(Why)] 인지(Perception) 이벤트들을 배칭하여 JSON 문자열로 변환합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static FString SerializePerceptionReport(const FString& AgentID, const TArray<FPerceptionData>& PerceptionEvents);

    /** location_decision_result 메시지 파싱.
     *  { "type": "location_decision_result", "payload": { "agent_id": "...", "chosen_id": "...", "request_gen": N } }
     *  request_gen 미포함 시 OutRequestGen=0 — stale 검사를 건너뜀(레거시 호환).
     *  @return true이면 OutAgentId / OutChosenId에 값이 채워짐 */
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static bool ParseLocationDecisionResult(
        const FString& Json, FString& OutAgentId, FString& OutChosenId, FString& OutReason, int32& OutRequestGen);

    static bool ParseLocationDecisionResultFromObject(
        const TSharedPtr<FJsonObject>& Root, FString& OutAgentId, FString& OutChosenId, FString& OutReason, int32& OutRequestGen);

    /** state_update 응답에서 relations 파싱.
     *  { "status": "cached", "agent_id": "...", "relations": [{"target_id":"...", "affinity_score":N, ...}] }
     *  @return true이면 OutAgentId와 OutRelations(TargetID→Score)에 값이 채워짐 */
    static bool ParseAffinityUpdate(
        const FString& Json, FString& OutAgentId, TMap<FString, int32>& OutRelations);

    static bool ParseAffinityUpdateFromObject(
        const TSharedPtr<FJsonObject>& Root, FString& OutAgentId, TMap<FString, int32>& OutRelations);

    /** npc_audio_response 메시지 파싱 (TTS 통합 계획서 §3).
     *  성공 시 OutNpcId/OutWsUrl/OutSampleRate/OutChannels/OutDialogue/OutEmotion 채움.
     *  type 이 "npc_audio_response" 가 아니면 false 반환 (조용히 패스). */
    static bool ParseNpcAudioResponseFromObject(
        const TSharedPtr<FJsonObject>& Root,
        FString& OutNpcId,
        FString& OutWsUrl,
        int32& OutSampleRate,
        int32& OutChannels,
        FString& OutDialogueText,
        FString& OutEmotion);
};
