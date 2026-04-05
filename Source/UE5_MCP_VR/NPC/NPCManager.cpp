#include "NPCManager.h"
#include "SmartNPC.h"
#include "../Network/MCPJsonUtils.h"
#include "../Network/EnvelopeBuilder.h"
#include "Engine/GameInstance.h"
#include "Struct/NPCActionKeys.h"
#include "Action/NPCActionComponent.h"


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


void UNPCMap::DeliverLocationDecision(const FString& AgentID, const FString& ChosenCandidateId)
{
    ASmartNPC* NPC = GetValidNPC(AgentID);
    if (!NPC)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCMap] DeliverLocationDecision - NPC '%s' 없음"), *AgentID);
        return;
    }
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        ActionComp->NotifyLocationDecisionReady(ChosenCandidateId);
    }
}

void UNPCMap::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Received WebSocket Payload (Size: %d bytes)"), JsonMessage.Len());

    // ── 사전 필터링: 서버의 단순 ACK / 에러 응답을 조용히 무시 ─────────────
    // WHY: 서버는 emergency_report, state_update 등에 대해
    //      {"status": "received", "msg_id": "..."} 형태의 ACK을 반환한다.
    //      이 응답은 NPC에게 실행시킬 액션이 없으므로, ModeActionRequest
    //      파서로 넘기기 전에 "ActionBatches" 필드 존재 여부로 걸러낸다.
    {
        TSharedPtr<FJsonObject> PreCheckObj;
        TSharedRef<TJsonReader<>> PreCheckReader = TJsonReaderFactory<>::Create(JsonMessage);
        if (!FJsonSerializer::Deserialize(PreCheckReader, PreCheckObj) || !PreCheckObj.IsValid())
        {
            UE_LOG(LogTemp, Error,
                TEXT("[NPCMap] Received non-JSON or malformed message. Raw (first 200 chars): %.200s"),
                *JsonMessage);
            return;
        }

        if (!PreCheckObj->HasField(TEXT("ActionBatches")))
        {
            // ActionBatches 없음 = ACK / 에러 / 메타 응답으로 간주하고 무시
            FString StatusValue;
            PreCheckObj->TryGetStringField(TEXT("status"), StatusValue);
            UE_LOG(LogTemp, Verbose,
                TEXT("[NPCMap] Non-action response received (status='%s'). Skipping dispatch."),
                *StatusValue);
            return;
        }
    }

    // ── 실제 ModeActionRequest 파싱 ────────────────────────────────────────
    FModeActionRequest ParsedRequest;
    const bool bIsParsedSuccessfully = UMCPJsonUtils::ParseModeActionRequest(JsonMessage, ParsedRequest);

    if (!bIsParsedSuccessfully)
    {
        // ActionBatches 필드는 있지만 내부 포맷이 잘못된 경우
        UE_LOG(LogTemp, Error,
            TEXT("[NPCMap] Failed to Parse ModeActionRequest! ActionBatches field exists but format is invalid.\nRaw (first 500 chars): %.500s"),
            *JsonMessage);
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
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] SLM 응답 수신 -> %s"), *JsonMessage);

    if (NPCMap)
    {
        NPCMap->OnWebSocketMessageReceived(JsonMessage);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Cannot route SLM response; NPCMap is not ready."));
    }
}

void UNPCManager::OnLLMMessageReceived(const FString& JsonMessage)
{
    if (!NPCMap)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Received LLM response but NPCMap is not initialized."));
        return;
    }

    // location_decision_result 메시지는 전술 위치 파이프라인으로 별도 라우팅
    // (ActionBatches 필드가 없으므로 일반 OnWebSocketMessageReceived에서는 무시됨)
    FString AgentID;
    FString ChosenCandidateId;
    if (UMCPJsonUtils::ParseLocationDecisionResult(JsonMessage, AgentID, ChosenCandidateId))
    {
        NPCMap->DeliverLocationDecision(AgentID, ChosenCandidateId);
        return;
    }

    NPCMap->OnWebSocketMessageReceived(JsonMessage);
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
