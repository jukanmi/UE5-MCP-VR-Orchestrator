#pragma once

#include "../AI/NPCActionTypes.h"
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
    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static bool ParseActionBatch(FString Json, FActionBatch& OutBatch);

    UFUNCTION(BlueprintCallable, Category = "MCP|Utils")
    static bool ParseActionBatchArray(FString Json, TArray<FActionBatch>& OutBatches);
};
