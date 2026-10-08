#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "POIManager.generated.h"

class APOIActor;

/**
 * 월드 POI 등록소(WorldSubsystem). APOIActor 의 BeginPlay/EndPlay 가 등록·해제한다.
 *
 * 소비처: NPCManager::CollectNearbyContext(jev_daily pois 풀).
 * 거리는 수평(2D) 기준 — 가구·아이템 인지 반경과 같은 기준.
 */
UCLASS()
class UE5_MCP_VR_API UPOIManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** 등록. PoiId 가 비었거나 다른 액터가 이미 쓰는 id 면 경고 후 거부(숨은 덮어쓰기 방지). */
    void Register(APOIActor* Poi);

    /** 해제. 등록된 액터가 이 액터일 때만 지운다. */
    void Unregister(APOIActor* Poi);

    /** id(정확히 일치) → POI. 없으면 nullptr. */
    APOIActor* FindById(const FString& PoiId) const;

    /** 별칭 또는 표시명이 키워드와 정확히 일치하는 첫 POI. 없으면 nullptr. */
    APOIActor* FindByAlias(const FString& Keyword) const;

    /** Origin 에서 수평 Radius(cm) 이내 POI 목록. */
    TArray<APOIActor*> GetInRadius(const FVector& Origin, float Radius) const;

    /** Type 이 일치하는 POI 목록. */
    TArray<APOIActor*> GetByType(const FString& Type) const;

    int32 Num() const { return Pois.Num(); }

private:
    UPROPERTY()
    TMap<FString, TObjectPtr<APOIActor>> Pois;
};
