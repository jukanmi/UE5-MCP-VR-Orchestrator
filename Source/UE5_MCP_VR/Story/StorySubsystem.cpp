#include "Story/StorySubsystem.h"
#include "Core/Utils/SubsystemUtils.h"

UStorySubsystem* UStorySubsystem::Get(const UObject* WorldContext)
{
    return SubsystemUtils::GetGameSubsystem<UStorySubsystem>(WorldContext);
}

bool UStorySubsystem::ApplyStoryJson(const TSharedPtr<FJsonObject>& StoryObj)
{
    // 키는 Python StoryMachine::story_block 과 1:1 — 내부 snake_case(NpcPlans 규약 동일).
    FStoryState State;
    if (!StoryObj.IsValid() || !StoryObj->TryGetStringField(TEXT("beat_id"), State.BeatId))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Story] Story 블록에 beat_id 없음 — 무시."));
        return false;
    }
    StoryObj->TryGetStringField(TEXT("quest_log"), State.QuestLog);

    const TArray<TSharedPtr<FJsonValue>>* SideArr = nullptr;
    if (StoryObj->TryGetArrayField(TEXT("side"), SideArr))
    {
        for (const TSharedPtr<FJsonValue>& Val : *SideArr)
        {
            FString Id;
            if (Val.IsValid() && Val->TryGetString(Id))
            {
                State.Side.Add(Id);
            }
        }
    }

    CurrentState = MoveTemp(State);
    bHasState = true;
    UE_LOG(LogTemp, Log, TEXT("[Story] 비트 갱신: %s quest_log=\"%s\" side=%d"),
        *CurrentState.BeatId, *CurrentState.QuestLog, CurrentState.Side.Num());
    OnStoryUpdated.Broadcast(CurrentState);
    return true;
}
