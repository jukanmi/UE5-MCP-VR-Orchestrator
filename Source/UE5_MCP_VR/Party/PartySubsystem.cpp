#include "Party/PartySubsystem.h"
#include "Core/Utils/SubsystemUtils.h"

UPartySubsystem* UPartySubsystem::Get(const UObject* WorldContext)
{
    return SubsystemUtils::GetGameSubsystem<UPartySubsystem>(WorldContext);
}

bool UPartySubsystem::Join(const FString& AgentID)
{
    if (AgentID.IsEmpty()) return false;
    if (Members.Contains(AgentID)) return true; // 멱등 — 변경 없음, 델리게이트도 발행하지 않는다

    if (Members.Num() >= MaxSize)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Party] %s 합류 거절 — 정원 %d명 가득"), *AgentID, MaxSize);
        return false;
    }

    Members.Add(AgentID);
    UE_LOG(LogTemp, Log, TEXT("[Party] %s 합류 (%d/%d)"), *AgentID, Members.Num(), MaxSize);
    OnPartyChanged.Broadcast(AgentID, true);
    return true;
}

void UPartySubsystem::Leave(const FString& AgentID)
{
    if (Members.Remove(AgentID) == 0) return;

    UE_LOG(LogTemp, Log, TEXT("[Party] %s 해산 (%d/%d)"), *AgentID, Members.Num(), MaxSize);
    OnPartyChanged.Broadcast(AgentID, false);
}
