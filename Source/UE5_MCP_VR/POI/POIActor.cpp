#include "POI/POIActor.h"
#include "POI/POIManager.h"
#include "Engine/World.h"

APOIActor::APOIActor()
{
    // 공간 로딩되면 스트림 아웃된 POI 가 등록소에서 사라진다.
    bIsSpatiallyLoaded = false;
}

void APOIActor::BeginPlay()
{
    Super::BeginPlay();
    if (UWorld* World = GetWorld())
    {
        if (UPOIManager* Mgr = World->GetSubsystem<UPOIManager>())
        {
            Mgr->Register(this);
        }
    }
}

void APOIActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UPOIManager* Mgr = World->GetSubsystem<UPOIManager>())
        {
            Mgr->Unregister(this);
        }
    }
    Super::EndPlay(EndPlayReason);
}
