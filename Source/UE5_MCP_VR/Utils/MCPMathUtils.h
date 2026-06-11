#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPMathUtils.generated.h"

UCLASS()
class UE5_MCP_VR_API UMCPMathUtils : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "MCP|Utils")
    static FVector ConvertToUnrealLocation(float X, float Y, float Z);
};
