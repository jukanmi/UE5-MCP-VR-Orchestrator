#include "NPCManager.h"
#include "SmartNPC.h"
#include "../Network/MCPJsonUtils.h"
#include "Engine/GameInstance.h"
#include "Struct/NPCActionKeys.h"
#include "TimerManager.h"
#include "Engine/World.h"

// Time-Slicing 타이머 간격: 0.5초마다 큐를 처리해 틱당 오버헤드를 분산합니다.
static constexpr float StateUpdateInterval = 0.5f;

// 한 번의 타이머 틱에 상태를 전송할 NPC 최대 수 (틱 당 1~2명만 처리)
static constexpr int32 MaxNPCsPerTick = 2;

void UNPCManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ActiveNPCs.Empty();
    StateUpdateQueue.Empty();

    // [Time-Slicing] 0.5초 반복 타이머로 NPC 상태 전송 큐를 순차적으로 드레인합니다.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            StateUpdateTimerHandle,
            this,
            &UNPCManager::ProcessStateUpdateQueue,
            StateUpdateInterval,
            true // 반복 타이머
        );
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Subsystem Initialized with Time-Slicing timer (%.1fs interval)."), StateUpdateInterval);
}

void UNPCManager::Deinitialize()
{
    // 종료 시 타이머를 정리해 메모리 누수를 방지합니다.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StateUpdateTimerHandle);
    }
    ActiveNPCs.Empty();
    StateUpdateQueue.Empty();
    ConnectedSocket = nullptr;
    Super::Deinitialize();
}

void UNPCManager::RegisterNPC(const FString& InAgentID, ASmartNPC* InNPC)
{
    // 무효한 NPC 포인터나 빈 ID가 Map에 들어가는 것을 사전에 방지합니다.
    if (!InNPC || InAgentID.IsEmpty())
    {
        return;
    }
    ActiveNPCs.Add(InAgentID, InNPC);

    // 등록 시 State Update 큐에도 추가해 초기 상태를 전송하도록 예약합니다.
    StateUpdateQueue.AddUnique(InAgentID);
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Registered NPC: %s"), *InAgentID);
}

void UNPCManager::UnregisterNPC(const FString& InAgentID)
{
    ActiveNPCs.Remove(InAgentID);
    StateUpdateQueue.Remove(InAgentID);
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Unregistered NPC: %s"), *InAgentID);
}

void UNPCManager::BindWebSocket(UWebSocketClient* InSocket)
{
    ConnectedSocket = InSocket;
    if (ConnectedSocket)
    {
        ConnectedSocket->OnMessageReceived.AddDynamic(this, &UNPCManager::OnWebSocketMessageReceived);
        // WebSocket 연결/해제 이벤트를 구독하여 Fallback 트리거 기반을 마련합니다.
        ConnectedSocket->OnConnectionChanged.AddDynamic(this, &UNPCManager::OnWebSocketConnectionChanged);
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Successfully Bound to WebSocket Pipeline"));
    }
}

void UNPCManager::OnWebSocketConnectionChanged(bool bIsConnected)
{
    // 연결 상태를 캐시에 저장합니다.
    bIsSocketConnected = bIsConnected;

    if (!bIsConnected)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] WebSocket Disconnected! setting all NPCs to Offline Fallback mode."));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] WebSocket Reconnected. Resuming online AI mode."));
    }

    // 등록된 모든 NPC의 Blackboard IsConnected 키를 일괄 갱신합니다.
    // BT의 Selector 노드가 이를 감지해 Local Fallback 서브트리로 자동 분기합니다.
    for (const auto& NPCPair : ActiveNPCs)
    {
        ASmartNPC* TargetNPC = NPCPair.Value;
        if (IsValid(TargetNPC))
        {
            TargetNPC->SetBlackboardBool(TEXT("IsConnected"), bIsConnected);
        }
    }
}

void UNPCManager::SendEventToMCP(const FString& JsonData)
{
    // 연결이 끊겨 있으면 전송하지 않습니다 (Offline Mode 중).
    if (ConnectedSocket && bIsSocketConnected)
    {
        ConnectedSocket->SendPrompt(JsonData);
    }
}

void UNPCManager::SendStateToMCP(const FGameStateData& StateData)
{
    if (!ConnectedSocket || !bIsSocketConnected)
    {
        return;
    }

    // FGameStateData → JSON 직렬화 후 전송합니다.
    FString SerializedJson = UMCPJsonUtils::SerializeGameState(StateData);
    if (!SerializedJson.IsEmpty())
    {
        ConnectedSocket->SendStateUpdate(SerializedJson);
    }
}

/**
 * [Time-Slicing] 0.5초마다 호출되어 큐에서 최대 2명의 NPC 상태 전송을 순차 처리합니다.
 * 이를 통해 다수의 NPC가 매 틱마다 동시에 네트워크 요청을 보내는 것을 방지합니다.
 */
void UNPCManager::ProcessStateUpdateQueue()
{
    // 오프라인 상태이거나 큐가 비어있으면 아무것도 하지 않습니다.
    if (!bIsSocketConnected || StateUpdateQueue.IsEmpty())
    {
        return;
    }

    int32 SendCount = 0;
    while (!StateUpdateQueue.IsEmpty() && SendCount < MaxNPCsPerTick)
    {
        // 큐 맨 앞의 AgentID를 꺼냅니다.
        FString AgentID = StateUpdateQueue[0];
        StateUpdateQueue.RemoveAt(0);

        ASmartNPC** FoundNPC = ActiveNPCs.Find(AgentID);
        if (FoundNPC && IsValid(*FoundNPC))
        {
            // NPC에게 자신의 현재 FGameStateData를 채워 반환하도록 요청합니다.
            (*FoundNPC)->CollectAndSendStateUpdate();
            SendCount++;
        }
    }

    // 큐가 비었으면 모든 등록된 NPC를 다시 큐에 추가해 순환 처리합니다.
    if (StateUpdateQueue.IsEmpty())
    {
        for (const auto& NPCPair : ActiveNPCs)
        {
            if (IsValid(NPCPair.Value))
            {
                StateUpdateQueue.AddUnique(NPCPair.Key);
            }
        }
    }
}

void UNPCManager::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Received JSON Payload (Size: %d bytes)"), JsonMessage.Len());

    FModeActionRequest ParsedRequest;
    const bool bIsParsedSuccessfully = UMCPJsonUtils::ParseModeActionRequest(JsonMessage, ParsedRequest);

    if (!bIsParsedSuccessfully)
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCManager] Failed to Parse Valid ModeActionRequest! Ensure Standard JSON Format."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Request Validated! Master Mode: %d, BatchCount: %d"),
        static_cast<int32>(ParsedRequest.Mode), ParsedRequest.ActionBatches.Num());

    for (const auto& BatchPair : ParsedRequest.ActionBatches)
    {
        const FActionBatch& CurrentBatch = BatchPair.Value;
        DispatchActionBatch(CurrentBatch);
    }
}

void UNPCManager::DispatchActionBatch(const FActionBatch& ActionBatch)
{
    const bool bIsBroadcastMessage = ActionBatch.AgentID.Equals(NPCActionKeys::Agent_Broadcast, ESearchCase::IgnoreCase);

    if (bIsBroadcastMessage)
    {
        BroadcastToAllNPCs(ActionBatch);
    }
    else
    {
        DeliverToSpecificNPC(ActionBatch.AgentID, ActionBatch);
    }
}

void UNPCManager::BroadcastToAllNPCs(const FActionBatch& ActionBatch)
{
    // TODO: 현재는 모든 등록된 NPC에게 일괄 전달하지만,
    //       발신 위치 기준 반경 1000 이내의 NPC에게만 전달하도록 변경해야 합니다.
    //       → ActionBatch에 발신 위치(Origin)를 포함시키거나,
    //         발화자(Speaker)의 위치를 기준으로 FVector::Dist() 필터링 로직 추가 필요.
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Broadcasting Batch to ALL Registered Entities (%d Total)"), ActiveNPCs.Num());

    for (const auto& NPCPair : ActiveNPCs)
    {
        ASmartNPC* TargetNPC = NPCPair.Value;
        if (IsValid(TargetNPC))
        {
            TargetNPC->ExecuteActionBatch(ActionBatch);
        }
    }
}

void UNPCManager::DeliverToSpecificNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch)
{
    ASmartNPC** FoundNPCPtr = ActiveNPCs.Find(TargetAgentID);

    if (FoundNPCPtr && IsValid(*FoundNPCPtr))
    {
        (*FoundNPCPtr)->ExecuteActionBatch(ActionBatch);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NPCManager] Skipping Dispatch! Target NPC '%s' is not registered or was destroyed."),
            *TargetAgentID);
    }
}
