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

void UNPCManager::HandleMessage(const FString& JsonMessage)
{
    // Parse Batch
    FActionBatch Batch;
    if (UMCPJsonUtils::ParseActionBatch(JsonMessage, Batch))
    {
        // For now, we ignore Batch.AgentID (or use it as a global filter)
        // and iterate through actions.
        for (const FGameAction& Action : Batch.Actions)
        {
            if (ASmartNPC** NPC = NPCMap.Find(Action.TargetID))
            {
                if (*NPC)
                {
                    (*NPC)->ProcessAction(Action);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("NPCManager: Target NPC '%s' not found."), *Action.TargetID);
            }
        }
    }
}
