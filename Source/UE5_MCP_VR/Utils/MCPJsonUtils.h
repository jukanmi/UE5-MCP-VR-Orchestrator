#pragma once

#include "../AI/NPCActionTypes.h"
#include "../AI/GameStateData.h"
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

    // UE5의 현재 월드 상태(FGameStateData)를 Python 백엔드로 전송할 JSON 봉투(Envelope)로 직렬화합니다.
    // msg_id는 호출 시마다 새로운 GUID로 자동 생성됩니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static FString SerializeGameState(const FGameStateData& StateData);
};
