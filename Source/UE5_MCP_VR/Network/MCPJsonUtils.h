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
    static FString SerializePerceptionReport(const TArray<FPerceptionData>& PerceptionEvents);
};
