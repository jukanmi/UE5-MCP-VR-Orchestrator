#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/WebSocketClient.h"
#include "NPCManager.generated.h"

class ASmartNPC;

/**
 * Global Router for NPC Actions.
 * Listens to WebSocket and dispatches actions to registered SmartNPCs.
 */
UCLASS()
class UE5_MCP_VR_API UNPCManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Lifecycle
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Registration (O(1) Map)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RegisterNPC(FString AgentID, ASmartNPC* NPC);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void UnregisterNPC(FString AgentID);

    // Network Binding
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void BindSocket(UWebSocketClient* Socket);

private:
    // Handles message from WebSocket
    UFUNCTION()
    void HandleMessage(const FString& JsonMessage);

    UPROPERTY()
    TMap<FString, ASmartNPC*> NPCMap;

    UPROPERTY()
    UWebSocketClient* BoundSocket;
};
