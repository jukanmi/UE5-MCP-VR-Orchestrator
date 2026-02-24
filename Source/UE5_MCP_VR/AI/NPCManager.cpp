#include "NPCManager.h"
#include "SmartNPC.h"
#include "../Utils/MCPJsonUtils.h"
#include "Engine/GameInstance.h"
#include "NPCActionKeys.h"

void UNPCManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ActiveNPCs.Empty();
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Subsystem Initialized"));
}

void UNPCManager::Deinitialize()
{
    ActiveNPCs.Empty();
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
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Registered NPC: %s"), *InAgentID);
}

void UNPCManager::UnregisterNPC(const FString& InAgentID)
{
    ActiveNPCs.Remove(InAgentID);
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Unregistered NPC: %s"), *InAgentID);
}

void UNPCManager::BindWebSocket(UWebSocketClient* InSocket)
{
    ConnectedSocket = InSocket;
    if (ConnectedSocket)
    {
        // 콜백 함수 이름을 직관적인 OnWebSocketMessageReceived로 변경해 코드 흐름 파악을 돕습니다.
        ConnectedSocket->OnMessageReceived.AddDynamic(this, &UNPCManager::OnWebSocketMessageReceived);
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Successfully Bound to WebSocket Pipeline"));
    }
}

void UNPCManager::SendEventToMCP(const FString& JsonData)
{
    if (ConnectedSocket)
    {
        ConnectedSocket->SendData(JsonData);
    }
}

void UNPCManager::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    // 오케스트레이터로부터 수신되는 Json 메시지가 매우 길 수 있으므로, 로그 도배를 막고자 Size만 기록합니다.
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Received JSON Payload (Size: %d bytes)"), JsonMessage.Len());

    FModeActionRequest ParsedRequest;
    
    // 복잡성을 줄이고자, 에이전트 다수 통신에는 단 하나의 표준 포맷(FModeActionRequest) 파싱만을 수행합니다. (Depth 단축)
    const bool bIsParsedSuccessfully = UMCPJsonUtils::ParseModeActionRequest(JsonMessage, ParsedRequest);
    
    if (!bIsParsedSuccessfully)
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCManager] Failed to Parse Valid ModeActionRequest! Ensure Standard JSON Format."));
        return; 
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Request Validated! Master Mode: %d, BatchCount: %d"), static_cast<int32>(ParsedRequest.Mode), ParsedRequest.ActionBatches.Num());

    for (const auto& BatchPair : ParsedRequest.ActionBatches)
    {
        const FActionBatch& CurrentBatch = BatchPair.Value;
        DispatchActionBatch(CurrentBatch);
    }
}

void UNPCManager::DispatchActionBatch(const FActionBatch& ActionBatch)
{
    // 다중 브로드캐스트 조건과, 특정 NPC 발송 조건을 명확히 함수로 분리해 유지보수성을 높입니다.
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
/**
 * 모든 NPC에게 ActionBatch를 전달합니다.
 */
void UNPCManager::BroadcastToAllNPCs(const FActionBatch& ActionBatch)
{
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
/**
 * 특정 NPC에게 ActionBatch를 전달합니다.
 */
void UNPCManager::DeliverToSpecificNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch)
{
    ASmartNPC** FoundNPCPtr = ActiveNPCs.Find(TargetAgentID);
    
    if (FoundNPCPtr && IsValid(*FoundNPCPtr))
    {
        (*FoundNPCPtr)->ExecuteActionBatch(ActionBatch);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Skipping Dispatch! Target NPC '%s' is not registered or was destroyed."), *TargetAgentID);
    }
}
