#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "../Network/WebSocketClient.h"
#include "Struct/NPCActionTypes.h"
#include "../Core/GameStateData.h"
#include "NPCManager.generated.h"

class ASmartNPC;
class FJsonObject;

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

    const TMap<FString, ASmartNPC*>& GetActiveNPCs() const { return ActiveNPCs; }

    void DeliverToNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch);

    /** LLM이 선택한 전술 위치 후보 ID를 해당 NPC의 ActionComponent로 전달 */
    void DeliverLocationDecision(const FString& AgentID, const FString& ChosenCandidateId, const FString& Reason = TEXT(""));

    /** 이미 파싱된 JSON에서 ModeActionRequest를 추출하여 NPC들에 분배. */
    void DeliverParsedActionBatches(const TSharedPtr<FJsonObject>& Root);

    void OnWebSocketMessageReceived(const FString& JsonMessage);

private:
    UPROPERTY()
    TMap<FString, ASmartNPC*> ActiveNPCs;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNPCResponseReceived, const FString&, NPCName, const FString&, Message);

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UNPCManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "MCP|Chat")
    FOnNPCResponseReceived OnNPCResponseReceived;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // === NPC 등록/해제 ===
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RegisterNPC(const FString& AgentID, ASmartNPC* NPC);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void UnregisterNPC(const FString& AgentID);

    // === 취합된 긴급 인지 이벤트 전송 (단일 LLM 채널, Python이 SLM/LLM 자동 라우팅) ===
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
    bool IsServerConnected() const;

    /** state_update 주기 (초). 모든 등록 NPC에 대해 이 주기로 Python에 상태 동기화. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI")
    float StateUpdateInterval = 3.0f;

private:
    UPROPERTY()
    UNPCMap* NPCMap;

    UPROPERTY()
    ULLMNetworkClient* LLMClient;

    FTimerHandle StateUpdateTimerHandle;

    /** state_update 응답 대기 중인 AgentID 집합 — 응답 전 재전송 차단용 */
    TSet<FString> PendingStateUpdateAgents;

    /** 모든 등록된 NPC에 대해 state_update를 Python으로 전송 (Python 응답에 relations 포함 → AffinityCache 갱신). */
    void TickStateUpdate();

    UFUNCTION()
    void OnLLMMessageReceived(const FString& JsonMessage);

    UFUNCTION()
    void HandleNPCDialogue(const FString& AgentID, const FString& DialogueText);
};
