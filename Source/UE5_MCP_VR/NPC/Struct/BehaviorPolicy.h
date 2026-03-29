#pragma once

#include "CoreMinimal.h"
#include "BehaviorPolicy.generated.h"

/**
 * Represents the high-level behavioral policy received from the Cognitive Engine (Python).
 * Matches schemas/behavior_policy.py
 */
USTRUCT(BlueprintType)
struct FBehaviorPolicy
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    FString TraceID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    int32 PolicyVersion = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    double IssuedAt = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    float TTL = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    FString TargetGuid; // Raw string from JSON, parsed into FGuid at runtime

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    int32 BaseSeed = 0;
    
    // Traits (0.0 - 1.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    float Aggression = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    float Fear = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    float Vigilance = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
    int32 PolicyFlags = 0;
};
