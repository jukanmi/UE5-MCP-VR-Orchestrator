#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Utils/MCPJsonUtils.h" // For FGameAction struct
#include "SmartNPC.generated.h"

UCLASS()
class UE5_MCP_VR_API ASmartNPC : public ACharacter
{
    GENERATED_BODY()

public:
    ASmartNPC();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // Unique ID for routing (e.g. "Guard_1")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI")
    FString AgentID;

    // Defines how to handle an action (Switch logic)
    // Hybrid: C++ parses params -> Calls BP Event
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    virtual void ProcessAction(const FGameAction& Action);

protected:
    // --- Blueprint Implementable Events (Engine Logic) ---

    // Move to location with speed
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteMove(FVector TargetLocation, float Speed);

    // Speak text
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteSpeak(const FString& Text);

    // Generic fallback or other actions (Attack, Interact)
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteGenericAction(const FString& ActionType, const FString& TargetID);
};
