#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "../Network/WebSocketClient.h"
#include "Struct/NPCActionTypes.h"
#include "../Core/GameStateData.h"
#include "NPCManager.generated.h"

class ASmartNPC;

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

    void DeliverToNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch);
    void ProcessStateUpdateQueue(int32 MaxNPCsPerTick);

    /** LLM이 선택한 전술 위치 후보 ID를 해당 NPC의 ActionComponent로 전달 */
    void DeliverLocationDecision(const FString& AgentID, const FString& ChosenCandidateId);

    void OnWebSocketMessageReceived(const FString& JsonMessage);

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

    // === NPC 등록/해제 ===
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RegisterNPC(const FString& AgentID, ASmartNPC* NPC);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void UnregisterNPC(const FString& AgentID);

    // === 취합된 긴급 인지 이벤트 전송 ===
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendEventReport(const FString& AgentID, const FString& CombinedPayload);

    // === 디버그: WebSocket 메시지 직접 주입 ===
    UFUNCTION(BlueprintCallable, Category = "MCP|Debug")
    void OnWebSocketMessageReceived(const FString& JsonMessage);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendEnvelopePromptToLLM(const FString& JsonData);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendStateToMCP(const FGameStateData& StateData);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendEnvelopePromptToSLM(const FString& JsonData);

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
