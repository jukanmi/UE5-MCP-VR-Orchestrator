#include "NPCManager.h"
#include "SmartNPC.h"
#include "NPCStateComponent.h"
#include "../Network/MCPJsonUtils.h"
#include "../Network/EnvelopeBuilder.h"
#include "Action/NPCActionComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"


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


void UNPCMap::DeliverLocationDecision(const FString& AgentID, const FString& ChosenCandidateId, const FString& Reason)
{
    ASmartNPC* NPC = GetValidNPC(AgentID);
    if (!NPC)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCMap] DeliverLocationDecision - NPC '%s' 없음"), *AgentID);
        return;
    }
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        ActionComp->NotifyLocationDecisionReady(ChosenCandidateId, Reason);
    }
}

void UNPCMap::DeliverParsedActionBatches(const TSharedPtr<FJsonObject>& Root)
{
    if (!Root.IsValid() || !Root->HasField(TEXT("ActionBatches")))
    {
        FString StatusValue;
        if (Root.IsValid()) Root->TryGetStringField(TEXT("status"), StatusValue);
        UE_LOG(LogTemp, Verbose,
            TEXT("[NPCMap] Non-action response received (status='%s'). Skipping dispatch."),
            *StatusValue);
        return;
    }

    FModeActionRequest ParsedRequest;
    if (!UMCPJsonUtils::ParseModeActionRequestFromObject(Root, ParsedRequest))
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCMap] Failed to Parse ModeActionRequest from JSON object."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Request Validated! Master Mode: %d, BatchCount: %d"),
        static_cast<int32>(ParsedRequest.Mode), ParsedRequest.ActionBatches.Num());

    for (const auto& BatchPair : ParsedRequest.ActionBatches)
    {
        DeliverToNPC(BatchPair.Key, BatchPair.Value);
    }
}

void UNPCMap::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Received WebSocket Payload (Size: %d bytes)"), JsonMessage.Len());

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonMessage);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[NPCMap] Received non-JSON or malformed message. Raw (first 200 chars): %.200s"),
            *JsonMessage);
        return;
    }

    DeliverParsedActionBatches(Root);
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

    // 주기적 state_update 시작 — Python으로부터 affinity 변화를 받아 AffinityCache 갱신
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            StateUpdateTimerHandle,
            FTimerDelegate::CreateUObject(this, &UNPCManager::TickStateUpdate),
            StateUpdateInterval, true, StateUpdateInterval);
    }
}

void UNPCManager::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StateUpdateTimerHandle);
    }

    if (LLMClient) LLMClient->Disconnect();

    NPCMap = nullptr;
    LLMClient = nullptr;

    Super::Deinitialize();
}

void UNPCManager::TickStateUpdate()
{
    if (!NPCMap || !LLMClient || !LLMClient->IsConnected()) return;

    // 이전 응답이 아직 안 온 NPC는 재전송 건너뜀 — 응답 적체로 인한 연속 갱신 방지
    for (const TPair<FString, ASmartNPC*>& Pair : NPCMap->GetActiveNPCs())
    {
        ASmartNPC* NPC = Pair.Value;
        if (!IsValid(NPC)) continue;

        const FString& AgentID = Pair.Key;
        if (PendingStateUpdateAgents.Contains(AgentID))
        {
            UE_LOG(LogTemp, Verbose, TEXT("[NPCManager] %s state_update 스킵 — 응답 대기 중"), *AgentID);
            continue;
        }

        FGameStateData StateData;
        StateData.OwnerAgentID = AgentID;
        StateData.OwnerLocation = NPC->GetActorLocation();
        StateData.CurrentMode = ENPCBehaviorMode::Common;

        PendingStateUpdateAgents.Add(AgentID);
        LLMClient->SendStateUpdate(StateData);
    }
}

void UNPCManager::RegisterNPC(const FString& AgentID, ASmartNPC* NPC)
{
    if (NPCMap && NPC)
    {
        NPCMap->RegisterNPC(AgentID, NPC);

        if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
        {
            ActionComp->OnNPCDialogue.AddDynamic(this, &UNPCManager::HandleNPCDialogue);
        }
    }
}

void UNPCManager::UnregisterNPC(const FString& AgentID)
{
    if (NPCMap)
    {
        if (ASmartNPC* NPC = NPCMap->GetValidNPC(AgentID))
        {
            if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
            {
                ActionComp->OnNPCDialogue.RemoveDynamic(this, &UNPCManager::HandleNPCDialogue);
            }
        }
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

void UNPCManager::OnLLMMessageReceived(const FString& JsonMessage)
{
    if (!NPCMap)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Received LLM response but NPCMap is not initialized."));
        return;
    }

    // 한 번만 deserialize → 모든 핸들러가 같은 FJsonObject를 공유.
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonMessage);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[NPCManager] Malformed LLM JSON. Raw (first 200): %.200s"), *JsonMessage);
        return;
    }

    // state_update 응답 — relations(호감도) 데이터가 포함된 경우 AffinityCache 갱신
    {
        FString AgentID;
        TMap<FString, int32> Relations;
        if (UMCPJsonUtils::ParseAffinityUpdateFromObject(Root, AgentID, Relations) && Relations.Num() > 0)
        {
            PendingStateUpdateAgents.Remove(AgentID);

            if (ASmartNPC* NPC = NPCMap->GetValidNPC(AgentID))
            {
                if (UNPCStateComponent* StateComp = NPC->GetStateComponent())
                {
                    int32 ChangedCount = 0;
                    for (const auto& Pair : Relations)
                    {
                        if (StateComp->AffinityCache.FindRef(Pair.Key) != Pair.Value)
                        {
                            StateComp->UpdateAffinity(Pair.Key, Pair.Value);
                            ++ChangedCount;
                        }
                    }
                }
            }
            return;
        }
    }

    // location_decision_result 메시지는 전술 위치 파이프라인으로 별도 라우팅
    {
        FString AgentID, ChosenCandidateId, Reason;
        if (UMCPJsonUtils::ParseLocationDecisionResultFromObject(Root, AgentID, ChosenCandidateId, Reason))
        {
            NPCMap->DeliverLocationDecision(AgentID, ChosenCandidateId, Reason);
            return;
        }

        // type은 location_decision_result인데 payload 파싱 실패 → WaitingLLM 고착 방지
        FString TypeStr;
        if (Root->TryGetStringField(TEXT("type"), TypeStr) && TypeStr == TEXT("location_decision_result"))
        {
            FString AgentIDFallback;
            const TSharedPtr<FJsonObject>* PayloadObj;
            if (Root->TryGetObjectField(TEXT("payload"), PayloadObj))
                (*PayloadObj)->TryGetStringField(TEXT("agent_id"), AgentIDFallback);

            if (!AgentIDFallback.IsEmpty())
            {
                if (ASmartNPC* NPC = NPCMap->GetValidNPC(AgentIDFallback))
                    if (UNPCActionComponent* AC = NPC->GetActionComponent())
                        AC->AbortTacticalQuery();
            }
            return;
        }
    }

    NPCMap->DeliverParsedActionBatches(Root);
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

void UNPCManager::HandleNPCDialogue(const FString& AgentID, const FString& DialogueText)
{
    OnNPCResponseReceived.Broadcast(AgentID, DialogueText);
}
