// File: NPCManager.h
// Purpose: Global Router for NPC Actions. 
//          - Listens to WebSocket and dispatches actions to registered SmartNPCs.
//          - Time-Slicing 기반으로 NPC 상태를 순차적으로 Python에 전송합니다.
//          - WebSocket 연결/해제 이벤트를 수신해 Blackboard의 IsConnected 키를 동기화합니다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "../Network/WebSocketClient.h"
#include "Struct/NPCActionTypes.h"
#include "../Core/GameStateData.h"
#include "NPCManager.generated.h"

class ASmartNPC;

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

    // FGameStateData 구조체를 JSON으로 직렬화하여 Python 백엔드로 전송합니다.
    // NPC가 자신의 상태를 직접 채워 호출합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendStateToMCP(const struct FGameStateData& StateData);

    // 다수 NPC의 긴급 상황(피격 등)을 서버 전송 전 큐(버퍼)에 담아둡니다 (Debouncing 목적).
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RegisterEmergencyEvent(const FString& InAgentID, const FString& EventType, const FString& Description);

    // 웹소켓으로부터 수신된 원시 JSON 메시지를 파싱하여 FModeActionRequest로 변환하고 처리합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void OnWebSocketMessageReceived(const FString& JsonMessage);

    // WebSocket 연결 상태 변화 시 호출되는 이벤트 핸들러.
    // bIsConnected=false 수신 시 모든 등록된 NPC의 Blackboard IsConnected 키를 다운시킵니다.
    UFUNCTION()
    void OnWebSocketConnectionChanged(bool bIsConnected);

private:
    UPROPERTY()
    TMap<FString, ASmartNPC*> ActiveNPCs;

    UPROPERTY()
    UWebSocketClient* ConnectedSocket;

    // 현재 WebSocket 연결 상태 캐시 (Blackboard IsConnected 키 동기화용)
    bool bIsSocketConnected = false;

    // [Emergency Debouncing] 긴급 이벤트 저장 구조체
    struct FEmergencyEventData
    {
        FString AgentID;
        FString EventType;
        FString Description;
    };
    
    // 0.2~0.5초 등 짧은 주기로 수집된 긴급 이벤트를 묶어놓는 큐
    TArray<FEmergencyEventData> EmergencyEventQueue;

    // 모인 긴급 이벤트들을 하나의 패킷(배열)으로 조립하여 대규모 보고
    void FlushEmergencyQueue();

    // [Time-Slicing] 다중 NPC를 매 틱에 한꺼번에 전송하지 않기 위한 전송 예약 큐
    TArray<FString> StateUpdateQueue;    // 전송 대기중인 AgentID 큐
    FTimerHandle StateUpdateTimerHandle; // 큐를 일정 간격으로 소비하는 타이머

    // 타이머 콜백: 큐 맨 앞에서 1~2명씩 뽑아 자신의 상태를 전송하도록 요청하고, 
    // 모아둔 긴급 이벤트도 병합(Flush) 전송합니다.
    void ProcessStateUpdateQueue();

    // 파싱된 ActionBatch를 수신 대상(전체 혹은 단일 NPC)에 맞게 올바른 계층으로 라우팅합니다.
    void DispatchActionBatch(const struct FActionBatch& ActionBatch);

    // Agent_Broadcast 지시어인 경우, 현재 맵에 등록된 모든 NPC에게 일괄 행동을 전달합니다.
    void BroadcastToAllNPCs(const struct FActionBatch& ActionBatch);

    // 특정 AgentID를 가진 단일 NPC만 찾아 행동을 지시합니다 (O(1) Map 탐색).
    void DeliverToSpecificNPC(const FString& TargetAgentID, const struct FActionBatch& ActionBatch);
};
