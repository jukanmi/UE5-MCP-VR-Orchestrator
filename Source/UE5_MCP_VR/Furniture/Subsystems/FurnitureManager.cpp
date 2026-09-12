#include "Furniture/Subsystems/FurnitureManager.h"
#include "Furniture/BP/FurnitureActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

void UFurnitureManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ActiveFurniture.Empty();
    UE_LOG(LogTemp, Log, TEXT("[FurnitureManager] Subsystem Initialized."));
}

void UFurnitureManager::Deinitialize()
{
    ActiveFurniture.Empty();
    Super::Deinitialize();
}

UFurnitureManager* UFurnitureManager::Get(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UFurnitureManager>() : nullptr;
}

void UFurnitureManager::RegisterFurniture(const FString& InFurnitureID, AFurnitureActor* InFurniture)
{
    if (!IsValid(InFurniture) || InFurnitureID.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[FurnitureManager] 등록 실패: 무효 액터 또는 빈 FurnitureID."));
        return;
    }

    if (ActiveFurniture.Contains(InFurnitureID))
    {
        UE_LOG(LogTemp, Warning, TEXT("[FurnitureManager] FurnitureID %s 중복 등록 — 덮어씀."), *InFurnitureID);
    }

    ActiveFurniture.Add(InFurnitureID, InFurniture);
    UE_LOG(LogTemp, Log, TEXT("[FurnitureManager] Registered: %s"), *InFurnitureID);
}

void UFurnitureManager::UnregisterFurniture(const FString& InFurnitureID)
{
    if (InFurnitureID.IsEmpty() || !ActiveFurniture.Contains(InFurnitureID))
    {
        return;
    }

    ActiveFurniture.Remove(InFurnitureID);
    UE_LOG(LogTemp, Log, TEXT("[FurnitureManager] Unregistered: %s"), *InFurnitureID);
}

AFurnitureActor* UFurnitureManager::GetFurnitureByID(const FString& InFurnitureID) const
{
    AFurnitureActor* const* Found = ActiveFurniture.Find(InFurnitureID);
    return (Found && IsValid(*Found)) ? *Found : nullptr;
}

AFurnitureActor* UFurnitureManager::FindNearestVacantSitable(const FVector& Location, float Range) const
{
    AFurnitureActor* Nearest = nullptr;
    float BestDistSq = FMath::Square(Range);
    for (const TPair<FString, AFurnitureActor*>& Pair : ActiveFurniture)
    {
        AFurnitureActor* Furniture = Pair.Value;
        if (!IsValid(Furniture) || Furniture->IsOccupied()) continue;

        const float DistSq = FVector::DistSquared2D(Furniture->GetActorLocation(), Location);
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Nearest = Furniture;
        }
    }
    return Nearest;
}
