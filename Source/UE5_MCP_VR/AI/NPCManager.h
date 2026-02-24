#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "../Network/WebSocketClient.h"
#include "NPCActionTypes.h"
#include "NPCManager.generated.h"

class ASmartNPC;

/**
 * Global Router for NPC Actions.
 * Listens to WebSocket and dispatches actions to registered SmartNPCs.
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UNPCManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Lifecycle
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Registration (O(1) Map)
    // FString 매개변수는 불필요한 복사를 막기 위해 const reference로 처리합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RegisterNPC(const FString& InAgentID, ASmartNPC* InNPC);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void UnregisterNPC(const FString& InAgentID);

    // Network Binding
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void BindWebSocket(UWebSocketClient* InSocket);

    // 특정 NPC 로직이나 엔진에서 오케스트레이터(MCP)로 응답할 때 보냅니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendEventToMCP(const FString& JsonData);

    // 웹소켓으로부터 수신된 원시 JSON 메시지를 파싱하여 FModeActionRequest로 변환하고 처리합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void OnWebSocketMessageReceived(const FString& JsonMessage);

private:
    UPROPERTY()
    TMap<FString, ASmartNPC*> ActiveNPCs;

    UPROPERTY()
    UWebSocketClient* ConnectedSocket;

    // 파싱된 ActionBatch를 수신 대상(전체 혹은 단일 NPC)에 맞게 올바른 계층으로 라우팅합니다.
    void DispatchActionBatch(const struct FActionBatch& ActionBatch);

    // Agent_Broadcast 지시어인 경우, 현재 맵에 등록된 모든 NPC에게 일괄 행동을 전달합니다.
    void BroadcastToAllNPCs(const struct FActionBatch& ActionBatch);

    // 특정 AgentID를 가진 단일 NPC만 찾아 행동을 지시합니다 (O(1) Map 탐색).
    void DeliverToSpecificNPC(const FString& TargetAgentID, const struct FActionBatch& ActionBatch);
};
