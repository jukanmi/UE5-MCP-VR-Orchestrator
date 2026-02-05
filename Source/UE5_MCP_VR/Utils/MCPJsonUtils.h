#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPJsonUtils.generated.h"

USTRUCT(BlueprintType)
struct FGameAction
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "MCP")
    FString ActionType;

    UPROPERTY(BlueprintReadWrite, Category = "MCP")
    FString TargetID;

    // We store all params as strings for flexibility (converted on demand)
    UPROPERTY(BlueprintReadWrite, Category = "MCP")
    TMap<FString, FString> Parameters;
};

USTRUCT(BlueprintType)
struct FActionBatch
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "MCP")
    FString AgentID;

    UPROPERTY(BlueprintReadWrite, Category = "MCP")
    TArray<FGameAction> Actions;
};

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
};
