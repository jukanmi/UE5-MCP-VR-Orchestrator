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

    // 스폰 이벤트 공통 준비: 이벤트에서 IdField·loc 을 읽고, 설정 맵에서 클래스를 찾아 로드하고, loc 태그 액터 위치를 구한다.
    // 하나라도 실패하면 이유를 경고로 남기고 nullptr.
    template <typename T>
    UClass* ResolveSpawn(UWorld* World, const FJsonObject& EventObj, const TCHAR* Cmd, const TCHAR* IdField,
                         const TMap<FString, TSoftClassPtr<T>>& ClassMap, FString& OutId, FVector& OutLocation)
    {
        if (!World) return nullptr;

        FString LocTag;
        if (!EventObj.TryGetStringField(IdField, OutId) || !EventObj.TryGetStringField(TEXT("loc"), LocTag))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] %s 이벤트 인자 누락 (%s 또는 loc)"), Cmd, IdField);
            return nullptr;
        }

        const TSoftClassPtr<T>* ClassPtr = ClassMap.Find(OutId);
        if (!ClassPtr || ClassPtr->IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] %s 실패: '%s' 에 해당하는 클래스가 StoryDirectorSettings 에 없음."), Cmd, *OutId);
            return nullptr;
        }

        UClass* LoadedClass = ClassPtr->LoadSynchronous();
        if (!LoadedClass)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] %s 실패: '%s' 클래스 로드 실패."), Cmd, *OutId);
            return nullptr;
        }

        if (!FindTaggedActorLocation(World, LocTag, OutLocation))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Story] %s 실패: '%s' 태그를 가진 액터를 찾을 수 없음."), Cmd, *LocTag);
            return nullptr;
        }
        return LoadedClass;
    }

    AActor* SpawnAt(UWorld* World, UClass* Class, const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        return World->SpawnActor<AActor>(Class, Location, FRotator::ZeroRotator, SpawnParams);
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

    const TSharedPtr<FJsonObject>* TitlesObj = nullptr;
    if (StoryObj->TryGetObjectField(TEXT("side_titles"), TitlesObj) && TitlesObj)
    {
        for (const auto& Pair : (*TitlesObj)->Values)
        {
            FString Title;
            if (Pair.Value.IsValid() && Pair.Value->TryGetString(Title)) State.SideTitles.Add(Pair.Key, Title);
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

AActor* UStorySubsystem::GetQuestTargetActor() const
{
    return QuestMarker ? QuestMarker->GetTarget() : nullptr;
}

void UStorySubsystem::ExecuteEvent(const TSharedPtr<FJsonObject>& EventObj)
{
    if (!EventObj.IsValid()) return;

    FString Cmd;
    if (!EventObj->TryGetStringField(TEXT("cmd"), Cmd) && !EventObj->TryGetStringField(TEXT("type"), Cmd))
    {
        return; // cmd(또는 type) 필드 없음
    }

    const UStoryDirectorSettings* Settings = GetDefault<UStoryDirectorSettings>();
    UWorld* World = GetWorld();

    if (Cmd == TEXT("spawn_enemy"))
    {
        FString EnemyId;
        FVector BaseLocation;
        UClass* LoadedClass = ResolveSpawn(World, *EventObj, *Cmd, TEXT("enemy_id"), Settings->EnemyClassMap, EnemyId, BaseLocation);
        if (!LoadedClass) return;
        int32 Count = 1;
        EventObj->TryGetNumberField(TEXT("count"), Count);

        UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
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

            SpawnAt(World, LoadedClass, SpawnLoc);
            UE_LOG(LogTemp, Log, TEXT("[Story] spawn_enemy: %s %d기 스폰 완료 (위치: %s)"), *EnemyId, Count, *SpawnLoc.ToString());
        }
    }
    else if (Cmd == TEXT("spawn_item"))
    {
        FString ItemId;
        FVector BaseLocation;
        UClass* LoadedClass = ResolveSpawn(World, *EventObj, *Cmd, TEXT("item_id"), Settings->ItemClassMap, ItemId, BaseLocation);
        if (!LoadedClass) return;
        int32 Count = 1;
        EventObj->TryGetNumberField(TEXT("count"), Count);

        for (int32 i = 0; i < Count; ++i)
        {
            SpawnAt(World, LoadedClass, BaseLocation);
            UE_LOG(LogTemp, Log, TEXT("[Story] spawn_item: %s"), *ItemId);
        }
    }
    else if (Cmd == TEXT("spawn_npc"))
    {
        FString NpcId;
        FVector BaseLocation;
        UClass* LoadedClass = ResolveSpawn(World, *EventObj, *Cmd, TEXT("npc_id"), Settings->NPCClassMap, NpcId, BaseLocation);
        if (!LoadedClass) return;

        SpawnAt(World, LoadedClass, BaseLocation);
        UE_LOG(LogTemp, Log, TEXT("[Story] spawn_npc: %s"), *NpcId);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[Story] 알 수 없는 이벤트 cmd: %s"), *Cmd);
    }
}
