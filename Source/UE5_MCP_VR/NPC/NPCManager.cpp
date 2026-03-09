#include "NPCManager.h"
#include "SmartNPC.h"
#include "../Network/MCPJsonUtils.h"
#include "../Network/EnvelopeBuilder.h"
#include "Engine/GameInstance.h"
#include "Struct/NPCActionKeys.h"


// --- UNPCMap ---


void UNPCMap::DeliverToNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch)
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


void UNPCMap::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Received Debug JSON Payload (Size: %d bytes)"), JsonMessage.Len());

    FModeActionRequest ParsedRequest;
    const bool bIsParsedSuccessfully = UMCPJsonUtils::ParseModeActionRequest(JsonMessage, ParsedRequest);

    if (!bIsParsedSuccessfully)
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCMap] Failed to Parse Valid ModeActionRequest! Ensure Standard JSON Format."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Request Validated! Master Mode: %d, BatchCount: %d"),
        static_cast<int32>(ParsedRequest.Mode), ParsedRequest.ActionBatches.Num());

    for (const auto& BatchPair : ParsedRequest.ActionBatches)
    {
        DeliverToNPC(BatchPair.Key, BatchPair.Value);
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
        LLMClient->InitializeLLM();
    }

    SLMClient = NewObject<USLMNetworkClient>(this);
    if (SLMClient)
    {
        SLMClient->OnMessageReceived.AddDynamic(this, &UNPCManager::OnSLMMessageReceived);
        SLMClient->InitializeSLM();
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

void UNPCManager::RegisterNPC(const FString& AgentID, ASmartNPC* NPC)
{
    if (NPCMap)
    {
        NPCMap->RegisterNPC(AgentID, NPC);
    }
}

void UNPCManager::UnregisterNPC(const FString& AgentID)
{
    if (NPCMap)
    {
        NPCMap->UnregisterNPC(AgentID);
    }
}

void UNPCManager::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    if (NPCMap)
    {
        NPCMap->OnWebSocketMessageReceived(JsonMessage);
    }
}


bool UNPCManager::IsServerConnected() const
{
    return LLMClient ? LLMClient->IsConnected() : false;
}

void UNPCManager::SendEnvelopePromptToLLM(const FString& JsonData)
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

void UNPCManager::SendEnvelopePromptToSLM(const FString& JsonData)
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
            NPCMap->DeliverToNPC(BatchPair.Key, BatchPair.Value);
        }
    }
}

void UNPCManager::SendEventReport(const FString& AgentID, const FString& CombinedPayload)
{
    // 이미 NPCStateComponent에서 취합/배치/JSON화가 끝난 데이터를 받음
    // 여기서는 Envelope 래핑만 해서 즉시 발송
    FString Envelope = FEnvelopeBuilder::BuildEmergencyReport(CombinedPayload);
    
    if (LLMClient && LLMClient->IsConnected())
    {
        LLMClient->SendPrompt(Envelope);
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Event Report Sent for Agent: %s"), *AgentID);
    }
}
