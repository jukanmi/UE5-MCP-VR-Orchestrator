#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "../Network/WebSocketClient.h"
#include "Struct/NPCActionTypes.h"
#include "../Core/GameStateData.h"
#include "NPCManager.generated.h"

class ASmartNPC;

// WebSocketClients moved to WebSocketClient.h

// NPC 관리를 담당하는 클래스
UCLASS(BlueprintType)
class UE5_MCP_VR_API UNPCMap : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RegisterNPC(const FString& InAgentID, ASmartNPC* InNPC){ ActiveNPCs.Add(InAgentID, InNPC); };

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void UnregisterNPC(const FString& InAgentID){ ActiveNPCs.Remove(InAgentID); };

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    ASmartNPC* GetValidNPC(const FString& AgentID) const{ return ActiveNPCs.FindRef(AgentID); };

    void DispatchActionBatch(const FActionBatch& ActionBatch);
    void BroadcastToAllNPCs(const FActionBatch& ActionBatch);
    void DeliverToSpecificNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch);
    void ProcessStateUpdateQueue(int32 MaxNPCsPerTick);

private:
    UPROPERTY()
    TMap<FString, ASmartNPC*> ActiveNPCs;

    UPROPERTY()
    TArray<FString> StateUpdateQueue;
};


UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UNPCManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;


    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void InitializeLLMClient();

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendPromptToLLM(const FString& JsonData);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendStateToMCP(const FGameStateData& StateData);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendPromptToSLM(const FString& JsonData);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    bool IsServerConnected() const;

private:
    UPROPERTY()
    UNPCMap* NPCMap;

    UPROPERTY()
    ULLMNetworkClient* LLMClient;

    UPROPERTY()
    USLMNetworkClient* SLMClient;



    UFUNCTION()
    void OnLLMMessageReceived(const FString& JsonMessage);

    UFUNCTION()
    void OnSLMMessageReceived(const FString& JsonMessage);

};
