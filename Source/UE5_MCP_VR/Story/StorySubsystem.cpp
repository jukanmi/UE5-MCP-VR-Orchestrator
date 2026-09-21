#include "Story/StorySubsystem.h"
#include "Core/Utils/SubsystemUtils.h"
#include "Story/StoryDirectorSettings.h"
#include "Enemy/EnemyCharacter.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "NPC/BP/SmartNPC.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Story/QuestMarkerActor.h"

namespace
{
    // LocTag 태그를 가진 첫 액터 위치. 비트 전이 때만 도는 경로라 전체 순회로 충분.
    bool FindTaggedActorLocation(UWorld* World, const FString& LocTag, FVector& OutLocation)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->ActorHasTag(FName(*LocTag)))
            {
                OutLocation = It->GetActorLocation();
                return true;
            }
        }
        return false;
    }
}

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
    StoryObj->TryGetStringField(TEXT("quest_target_tag"), State.QuestTargetTag);

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

    const TArray<TSharedPtr<FJsonValue>>* AvailArr = nullptr;
    if (StoryObj->TryGetArrayField(TEXT("available_side"), AvailArr))
    {
        for (const TSharedPtr<FJsonValue>& Val : *AvailArr)
        {
            FString Id;
            if (Val.IsValid() && Val->TryGetString(Id))
            {
                State.AvailableSide.Add(Id);
            }
        }
    }

    CurrentState = MoveTemp(State);
    bHasState = true;
    UE_LOG(LogTemp, Log, TEXT("[Story] 비트 갱신: %s quest_log=\"%s\" side=%d target=%s"),
        *CurrentState.BeatId, *CurrentState.QuestLog, CurrentState.Side.Num(), *CurrentState.QuestTargetTag);
    SetQuestTarget(CurrentState.QuestTargetTag);
    OnStoryUpdated.Broadcast(CurrentState);

    // 이벤트 파싱 (Phase C)
    const TArray<TSharedPtr<FJsonValue>>* EventsArr = nullptr;
    if (StoryObj->TryGetArrayField(TEXT("events"), EventsArr))
    {
        for (const TSharedPtr<FJsonValue>& EventVal : *EventsArr)
        {
            if (EventVal.IsValid() && EventVal->Type == EJson::Object)
            {
                ExecuteEvent(EventVal->AsObject());
            }
        }
    }

    return true;
}

void UStorySubsystem::SetQuestTarget(const FString& Tag)
{
    UWorld* World = GetWorld();
    if (!World) return;

    AActor* Target = nullptr;
    if (!Tag.IsEmpty())
    {
        if (UNPCManager* Manager = UNPCManager::Get(this))
        {
            Target = Manager->GetNPCById(Tag);
        }
        if (!Target)
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->ActorHasTag(FName(*Tag))) { Target = *It; break; }
            }
        }
        if (!Target)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] quest_target_tag '%s' — AgentID·Tag 모두 미발견, 마커 숨김"), *Tag);
        }
    }

    // 마커는 월드당 1개 — PIE 재시작 등으로 월드가 바뀌면 이전 포인터는 버린다.
    if (QuestMarker && QuestMarker->GetWorld() != World) QuestMarker = nullptr;
    if (!QuestMarker && Target)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        QuestMarker = World->SpawnActor<AQuestMarkerActor>(AQuestMarkerActor::StaticClass(), FTransform::Identity, Params);
    }
    if (QuestMarker) QuestMarker->SetTarget(Target);
}

void UStorySubsystem::ExecuteEvent(const TSharedPtr<FJsonObject>& EventObj)
{
    if (!EventObj.IsValid()) return;

    FString Cmd;
    if (!EventObj->TryGetStringField(TEXT("cmd"), Cmd) && !EventObj->TryGetStringField(TEXT("type"), Cmd))
    {
        return; // cmd(또는 type) 필드 없음
    }

    if (Cmd == TEXT("spawn_enemy"))
    {
        FString EnemyId, LocTag;
        int32 Count = 1;

        if (!EventObj->TryGetStringField(TEXT("enemy_id"), EnemyId) || !EventObj->TryGetStringField(TEXT("loc"), LocTag))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] spawn_enemy 이벤트 인자 누락 (enemy_id 또는 loc)"));
            return;
        }
        EventObj->TryGetNumberField(TEXT("count"), Count);

        const UStoryDirectorSettings* Settings = GetDefault<UStoryDirectorSettings>();
        if (!Settings) return;

        const TSoftClassPtr<AEnemyCharacter>* ClassPtr = Settings->EnemyClassMap.Find(EnemyId);
        if (!ClassPtr || ClassPtr->IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] spawn_enemy 실패: '%s' 에 해당하는 EnemyClass 가 Settings 에 없음."), *EnemyId);
            return;
        }

        UClass* LoadedClass = ClassPtr->LoadSynchronous();
        if (!LoadedClass) return;

        UWorld* World = GetWorld();
        if (!World) return;

        FVector BaseLocation;
        const bool bFoundLoc = FindTaggedActorLocation(World, LocTag, BaseLocation);

        if (!bFoundLoc)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] spawn_enemy 실패: '%s' 태그를 가진 액터를 찾을 수 없음."), *LocTag);
            return;
        }

        UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        for (int32 i = 0; i < Count; ++i)
        {
            FVector SpawnLoc = BaseLocation;
            if (NavSys)
            {
                FNavLocation ProjectedLoc;
                if (NavSys->GetRandomReachablePointInRadius(BaseLocation, Settings->SpawnRadius, ProjectedLoc))
                {
                    SpawnLoc = ProjectedLoc.Location;
                }
            }

            World->SpawnActor<AActor>(LoadedClass, SpawnLoc, FRotator::ZeroRotator, SpawnParams);
            UE_LOG(LogTemp, Log, TEXT("[Story] spawn_enemy: %s %d기 스폰 완료 (위치: %s)"), *EnemyId, Count, *SpawnLoc.ToString());
        }
    }
    else if (Cmd == TEXT("spawn_item"))
    {
        FString ItemId, LocTag;
        int32 Count = 1;

        if (!EventObj->TryGetStringField(TEXT("item_id"), ItemId) || !EventObj->TryGetStringField(TEXT("loc"), LocTag))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] spawn_item parameter missing"));
            return;
        }
        EventObj->TryGetNumberField(TEXT("count"), Count);

        const UStoryDirectorSettings* Settings = GetDefault<UStoryDirectorSettings>();
        if (!Settings) return;

        const TSoftClassPtr<ADroppedItemBase>* ClassPtr = Settings->ItemClassMap.Find(ItemId);
        if (!ClassPtr || ClassPtr->IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] spawn_item fail: no ItemClassMap for %s"), *ItemId);
            return;
        }

        UClass* LoadedClass = ClassPtr->LoadSynchronous();
        if (!LoadedClass) return;

        UWorld* World = GetWorld();
        if (!World) return;

        FVector BaseLocation;
        const bool bFoundLoc = FindTaggedActorLocation(World, LocTag, BaseLocation);

        if (!bFoundLoc) return;

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        for (int32 i = 0; i < Count; ++i)
        {
            World->SpawnActor<AActor>(LoadedClass, BaseLocation, FRotator::ZeroRotator, SpawnParams);
            UE_LOG(LogTemp, Log, TEXT("[Story] spawn_item: %s"), *ItemId);
        }
    }
    else if (Cmd == TEXT("spawn_npc"))
    {
        FString NpcId, LocTag;
        if (!EventObj->TryGetStringField(TEXT("npc_id"), NpcId) || !EventObj->TryGetStringField(TEXT("loc"), LocTag))
        {
            return;
        }

        const UStoryDirectorSettings* Settings = GetDefault<UStoryDirectorSettings>();
        if (!Settings) return;

        const TSoftClassPtr<ASmartNPC>* ClassPtr = Settings->NPCClassMap.Find(NpcId);
        if (!ClassPtr || ClassPtr->IsNull()) return;

        UClass* LoadedClass = ClassPtr->LoadSynchronous();
        if (!LoadedClass) return;

        UWorld* World = GetWorld();
        if (!World) return;

        FVector BaseLocation;
        const bool bFoundLoc = FindTaggedActorLocation(World, LocTag, BaseLocation);

        if (!bFoundLoc) return;

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        World->SpawnActor<AActor>(LoadedClass, BaseLocation, FRotator::ZeroRotator, SpawnParams);
        UE_LOG(LogTemp, Log, TEXT("[Story] spawn_npc: %s"), *NpcId);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[Story] 알 수 없는 이벤트 cmd: %s"), *Cmd);
    }
}
