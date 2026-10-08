#include "POI/POIManager.h"
#include "POI/POIActor.h"

void UPOIManager::Register(APOIActor* Poi)
{
    if (!IsValid(Poi)) return;
    if (Poi->PoiId.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[POIManager] PoiId 가 빈 POI 는 등록하지 않음: %s"), *Poi->GetName());
        return;
    }
    if (const TObjectPtr<APOIActor>* Existing = Pois.Find(Poi->PoiId))
    {
        if (IsValid(*Existing) && *Existing != Poi)
        {
            UE_LOG(LogTemp, Warning, TEXT("[POIManager] PoiId '%s' 중복 — %s 는 등록하지 않음(기존 %s 유지)"),
                *Poi->PoiId, *Poi->GetName(), *(*Existing)->GetName());
            return;
        }
    }
    Pois.Add(Poi->PoiId, Poi);
    UE_LOG(LogTemp, Log, TEXT("[POIManager] POI 등록 '%s' (현재 %d개)"), *Poi->PoiId, Pois.Num());
}

void UPOIManager::Unregister(APOIActor* Poi)
{
    if (!Poi) return;
    const TObjectPtr<APOIActor>* Existing = Pois.Find(Poi->PoiId);
    if (Existing && *Existing == Poi)
    {
        Pois.Remove(Poi->PoiId);
    }
}

APOIActor* UPOIManager::FindById(const FString& PoiId) const
{
    const TObjectPtr<APOIActor>* Found = Pois.Find(PoiId);
    return (Found && IsValid(*Found)) ? Found->Get() : nullptr;
}

APOIActor* UPOIManager::FindByAlias(const FString& Keyword) const
{
    if (Keyword.IsEmpty()) return nullptr;
    for (const TPair<FString, TObjectPtr<APOIActor>>& Pair : Pois)
    {
        APOIActor* Poi = Pair.Value;
        if (!IsValid(Poi)) continue;
        if (Poi->DisplayName.ToString().Equals(Keyword) || Poi->Aliases.Contains(Keyword)) return Poi;
    }
    return nullptr;
}

TArray<APOIActor*> UPOIManager::GetInRadius(const FVector& Origin, float Radius) const
{
    TArray<APOIActor*> Result;
    for (const TPair<FString, TObjectPtr<APOIActor>>& Pair : Pois)
    {
        APOIActor* Poi = Pair.Value;
        if (IsValid(Poi) && FVector::Dist2D(Poi->GetActorLocation(), Origin) <= Radius) Result.Add(Poi);
    }
    return Result;
}

TArray<APOIActor*> UPOIManager::GetByType(const FString& Type) const
{
    TArray<APOIActor*> Result;
    for (const TPair<FString, TObjectPtr<APOIActor>>& Pair : Pois)
    {
        APOIActor* Poi = Pair.Value;
        if (IsValid(Poi) && Poi->Type == Type) Result.Add(Poi);
    }
    return Result;
}
