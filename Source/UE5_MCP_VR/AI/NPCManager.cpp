#include "NPCManager.h"
#include "SmartNPC.h"
#include "../Utils/MCPJsonUtils.h"
#include "Engine/GameInstance.h"

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
    // Parse Batch
    FActionBatch Batch;
    if (UMCPJsonUtils::ParseActionBatch(JsonMessage, Batch))
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Received ActionBatch for: %s"), *Batch.AgentID);
        
        // Check for broadcast mode
        if (Batch.AgentID.Equals(TEXT("broadcast"), ESearchCase::IgnoreCase))
        {
            // Send to ALL registered NPCs
            UE_LOG(LogTemp, Log, TEXT("[NPCManager] Broadcasting to %d NPCs"), NPCMap.Num());
            
            for (auto& Pair : NPCMap)
            {
                if (Pair.Value)
                {
                    for (const FGameAction& Action : Batch.Actions)
                    {
                        Pair.Value->ProcessAction(Action);
                    }
                }
            }
        }
        else
        {
            // Find the NPC by Batch.AgentID (e.g., "Elara")
            if (ASmartNPC** NPC = NPCMap.Find(Batch.AgentID))
            {
                if (*NPC)
                {
                    // Process all actions for this NPC
                    for (const FGameAction& Action : Batch.Actions)
                    {
                        (*NPC)->ProcessAction(Action);
                    }
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[NPCManager] NPC '%s' not found in registry!"), *Batch.AgentID);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Failed to parse ActionBatch from message"));
    }
}
