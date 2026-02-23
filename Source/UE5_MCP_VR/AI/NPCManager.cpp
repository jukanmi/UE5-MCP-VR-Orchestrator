#include "NPCManager.h"
#include "SmartNPC.h"
#include "../Utils/MCPJsonUtils.h"
#include "Engine/GameInstance.h"
#include "NPCActionKeys.h"

void UNPCManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    NPCMap.Empty();
    UE_LOG(LogTemp, Log, TEXT("NPCManager Subsystem Initialized"));
}

void UNPCManager::Deinitialize()
{
    NPCMap.Empty();
    BoundSocket = nullptr;
    Super::Deinitialize();
}

void UNPCManager::RegisterNPC(FString AgentID, ASmartNPC* NPC)
{
    if (NPC)
    {
        NPCMap.Add(AgentID, NPC);
        UE_LOG(LogTemp, Log, TEXT("Registered NPC: %s"), *AgentID);
    }
}

void UNPCManager::UnregisterNPC(FString AgentID)
{
    NPCMap.Remove(AgentID);
    UE_LOG(LogTemp, Log, TEXT("Unregistered NPC: %s"), *AgentID);
}

void UNPCManager::BindSocket(UWebSocketClient* Socket)
{
    BoundSocket = Socket;
    if (BoundSocket)
    {
        BoundSocket->OnMessageReceived.AddDynamic(this, &UNPCManager::HandleMessage);
        UE_LOG(LogTemp, Log, TEXT("NPCManager Bound to WebSocket"));
    }
}

void UNPCManager::SendEvent(const FString& JsonData)
{
    if (BoundSocket)
    {
        BoundSocket->SendData(JsonData);
    }
}

void UNPCManager::HandleMessage(const FString& JsonMessage)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] HandleMessage Received: %s"), *JsonMessage);

    // [Standard] FModeActionRequest 규격만 지원 (멀티 에이전트 오케스트레이션)
    FModeActionRequest Request;
    if (UMCPJsonUtils::ParseModeActionRequest(JsonMessage, Request))
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Parsed ModeActionRequest. Mode: %d, Batches: %d"), (int32)Request.Mode, Request.ActionBatches.Num());
        for (auto& Pair : Request.ActionBatches)
        {
            ProcessActionBatch(Pair.Value);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCManager] Failed to parse message! Invalid JSON structure or Non-Standard format."));
    }
}

void UNPCManager::ProcessActionBatch(const FActionBatch& Batch)
{
    // Case A: Broadcast (All NPCs)
    if (Batch.AgentID.Equals(NPCActionKeys::Agent_Broadcast, ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Broadcasting to %d NPCs"), NPCMap.Num());
        for (auto& Pair : NPCMap)
        {
            if (ASmartNPC* NPC = Pair.Value)
            {
                NPC->ExecuteActionBatch(Batch);
            }
        }
        return;
    }

    // Case B: Targeted (Specific NPC)
    if (ASmartNPC** NPC = NPCMap.Find(Batch.AgentID))
    {
        if (ASmartNPC* ValidNPC = *NPC)
        {
            ValidNPC->ExecuteActionBatch(Batch);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] NPC '%s' not registered!"), *Batch.AgentID);
    }
}
