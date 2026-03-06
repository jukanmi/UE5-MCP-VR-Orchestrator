#include "NPCManager.h"
#include "SmartNPC.h"
#include "../Network/MCPJsonUtils.h"
#include "../Network/EnvelopeBuilder.h"
#include "Engine/GameInstance.h"
#include "Struct/NPCActionKeys.h"

// --- ULLMNetworkClient ---

void ULLMNetworkClient::BindWebSocket(UWebSocketClient* InSocket)
{
    if (!InSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("[LLMNetworkClient] BindWebSocket: Invalid WebSocket pointer."));
        return;
    }
    Socket = InSocket;
    Socket->OnMessageReceived.AddDynamic(this, &ULLMNetworkClient::OnMessageReceivedHandler);
    Socket->OnConnectionChanged.AddDynamic(this, &ULLMNetworkClient::OnConnectionChangedHandler);
    UE_LOG(LogTemp, Log, TEXT("[LLMNetworkClient] Successfully Bound to LLM WebSocket Pipeline"));
}

void ULLMNetworkClient::SendPrompt(const FString& JsonData)
{
    if (bIsServerConnected && Socket)
    {
        Socket->SendPrompt(JsonData);
    }
}

void ULLMNetworkClient::SendStateUpdate(const FGameStateData& StateData)
{
    if (!bIsServerConnected || !Socket)
    {
        return;
    }

    FString PayloadJson = UMCPJsonUtils::SerializeGameState(StateData);
    if (PayloadJson.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[LLMNetworkClient] SendStateUpdate: Payload Json serialization failed."));
        return;
    }
    
    FString FinalEnvelopeJson = FEnvelopeBuilder::BuildStateUpdate(PayloadJson);
    if (FinalEnvelopeJson.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[LLMNetworkClient] SendStateUpdate: Final Envelope Json serialization failed."));
        return;
    }
    Socket->SendStateUpdate(FinalEnvelopeJson);
}

void ULLMNetworkClient::OnMessageReceivedHandler(const FString& Message)
{
    OnMessageReceived.Broadcast(Message);
}

void ULLMNetworkClient::OnConnectionChangedHandler(bool bIsConnected)
{
    bIsServerConnected = bIsConnected;
    OnConnectionChanged.Broadcast(bIsConnected);
}

// --- USLMNetworkClient ---

void USLMNetworkClient::Initialize()
{
    Socket = NewObject<UWebSocketClient>(this);
    if (Socket)
    {
        Socket->OnMessageReceived.AddDynamic(this, &USLMNetworkClient::OnMessageReceivedHandler);
        Socket->OnConnectionChanged.AddDynamic(this, &USLMNetworkClient::OnConnectionChangedHandler);
        Socket->Initialize(SLMWebSocketURL);
        UE_LOG(LogTemp, Log, TEXT("[SLMNetworkClient] SLM WebSocket initialized. URL: %s"), *SLMWebSocketURL);
    }
}

void USLMNetworkClient::SendPrompt(const FString& JsonData)
{
    if (!Socket)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SLMNetworkClient] SendPrompt: SLM socket is not initialized."));
        return;
    }
    Socket->SendSLMPrompt(JsonData);
}

void USLMNetworkClient::OnMessageReceivedHandler(const FString& Message)
{
    OnMessageReceived.Broadcast(Message);
}

// --- UNPCMap ---

void UNPCMap::DispatchActionBatch(const FActionBatch& ActionBatch)
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

void UNPCMap::BroadcastToAllNPCs(const FActionBatch& ActionBatch)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Broadcasting Batch to ALL Registered Entities (%d Total)"), ActiveNPCs.Num());

    for (const auto& NPCPair : ActiveNPCs)
    {
        if (IsValid(NPCPair.Value))
        {
            NPCPair.Value->ExecuteActionBatch(ActionBatch);
        }
    }
}

void UNPCMap::DeliverToSpecificNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch)
{
    if (ASmartNPC* TargetNPC = GetValidNPC(TargetAgentID))
    {
        TargetNPC->ExecuteActionBatch(ActionBatch);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCMap] Skipping Dispatch! Target NPC '%s' is not valid."), *TargetAgentID);
    }
}

void UNPCMap::ProcessStateUpdateQueue(int32 MaxNPCsPerTick)
{
    if (StateUpdateQueue.IsEmpty()) return;

    int32 SendCount = 0;
    while (!StateUpdateQueue.IsEmpty() && SendCount < MaxNPCsPerTick)
    {
        FString AgentID = StateUpdateQueue[0];
        StateUpdateQueue.RemoveAt(0);

        if (ASmartNPC* ValidNPC = GetValidNPC(AgentID))
        {
            ValidNPC->CollectAndSendStateUpdate();
            SendCount++;
        }
    }

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

// --- UNPCManager ---

void UNPCManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    NPCMap = NewObject<UNPCMap>(this);

    LLMClient = NewObject<ULLMNetworkClient>(this);
    if (LLMClient)
    {
        LLMClient->OnMessageReceived.AddDynamic(this, &UNPCManager::OnLLMMessageReceived);
        LLMClient->OnConnectionChanged.AddDynamic(this, &UNPCManager::OnWebSocketConnectionChanged);
    }

    SLMClient = NewObject<USLMNetworkClient>(this);
    if (SLMClient)
    {
        SLMClient->OnMessageReceived.AddDynamic(this, &UNPCManager::OnSLMMessageReceived);
        SLMClient->Initialize(); // Update initialization
    }
}

void UNPCManager::Deinitialize()
{
    if (LLMClient) LLMClient->Disconnect();
    if (SLMClient) SLMClient->Disconnect();

    NPCMap = nullptr;
    LLMClient = nullptr;
    SLMClient = nullptr;

    Super::Deinitialize();
}

void UNPCManager::BindWebSocket(UWebSocketClient* InSocket)
{
    if (LLMClient)
    {
        LLMClient->BindWebSocket(InSocket);
    }
}

void UNPCManager::OnWebSocketConnectionChanged(bool bIsConnected)
{
    if (!bIsConnected)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] WebSocket Disconnected! setting all NPCs to Offline Fallback mode."));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] WebSocket Reconnected. Resuming online AI mode."));
    }
}

bool UNPCManager::IsServerConnected() const
{
    return LLMClient ? LLMClient->IsConnected() : false;
}

void UNPCManager::SendPromptToLLM(const FString& JsonData)
{
    if (LLMClient)
    {
        LLMClient->SendPrompt(JsonData);
    }
}

void UNPCManager::SendStateToMCP(const FGameStateData& StateData)
{
    if (LLMClient)
    {
        LLMClient->SendStateUpdate(StateData);
    }
}

void UNPCManager::SendPromptToSLM(const FString& JsonData)
{
    if (SLMClient)
    {
        SLMClient->SendPrompt(JsonData);
    }
}

void UNPCManager::OnSLMMessageReceived(const FString& JsonMessage)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] SLM 응답 수신 (TODO: 응답 양식 미확정) -> %s"), *JsonMessage);
    // TODO: 응답 포맷 확정 후 파싱 및 NPC 즉각 리액션 로직 구현
}
// ProcessStateUpdateQueue removed - this logic is in UNPCMap now

void UNPCManager::OnLLMMessageReceived(const FString& JsonMessage)
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
        if (NPCMap)
        {
            NPCMap->DispatchActionBatch(BatchPair.Value);
        }
    }
}
