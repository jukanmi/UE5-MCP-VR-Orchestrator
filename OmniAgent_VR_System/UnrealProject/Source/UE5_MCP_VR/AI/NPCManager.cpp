#include "NPCManager.h"
#include "SmartNPC.h"
#include "Utils/MCPJsonUtils.h"

void UNPCManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("NPCManager Subsystem Initialized."));
}

void UNPCManager::Deinitialize()
{
    if (BoundSocket)
    {
        BoundSocket->OnMessageReceived.RemoveDynamic(this, &UNPCManager::HandleMessage);
    }
    Super::Deinitialize();
}

void UNPCManager::RegisterNPC(FString AgentID, ASmartNPC* NPC)
{
    if (!NPC) return;

    if (NPCMap.Contains(AgentID))
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Overwriting NPC Registration for ID: %s"), *AgentID);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Registered NPC: %s"), *AgentID);
    }

    NPCMap.Add(AgentID, NPC);
}

void UNPCManager::UnregisterNPC(FString AgentID)
{
    if (NPCMap.Remove(AgentID) > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Unregistered NPC: %s"), *AgentID);
    }
}

void UNPCManager::BindSocket(UWebSocketClient* Socket)
{
    if (!Socket) return;

    BoundSocket = Socket;
    // Bind to the Broadcast Delegate
    BoundSocket->OnMessageReceived.AddDynamic(this, &UNPCManager::HandleMessage);
    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Bound to WebSocket Client."));
}

void UNPCManager::HandleMessage(const FString& JsonMessage)
{
    // 1. Parse JSON -> ActionBatch
    FActionBatch Batch;
    if (!UMCPJsonUtils::ParseActionBatch(JsonMessage, Batch))
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCManager] Failed to parse ActionBatch!"));
        return;
    }

    // 2. Find Agent
    ASmartNPC** FoundNPC = NPCMap.Find(Batch.AgentID);
    if (!FoundNPC || !(*FoundNPC))
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Action received for unknown AgentID: %s"), *Batch.AgentID);
        return;
    }

    ASmartNPC* NPC = *FoundNPC;

    // 3. Process Actions
    for (const FGameAction& Action : Batch.Actions)
    {
        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Dispatching Action %s to %s"), *Action.ActionType, *Batch.AgentID);
        NPC->ProcessAction(Action);
    }
}
