#include "FurnitureActor.h"
#include "FurnitureManager.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"

AFurnitureActor::AFurnitureActor()
{
    // 정적 배치 가구 — 틱 불필요.
    PrimaryActorTick.bCanEverTick = false;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    SeatPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SeatPoint"));
    SeatPoint->SetupAttachment(RootComponent);
}

FTransform AFurnitureActor::GetSeatTransform() const
{
    return SeatPoint ? SeatPoint->GetComponentTransform() : GetActorTransform();
}

bool AFurnitureActor::TryOccupy(AActor* Occupant)
{
    if (!IsValid(Occupant)) return false;

    AActor* Current = OccupantWeak.Get();
    if (Current && Current != Occupant)
    {
        return false; // 타인 점유 중
    }

    OccupantWeak = Occupant;
    bOccupied = true;
    return true;
}

void AFurnitureActor::Release(AActor* Occupant)
{
    // 실점유자만 해제 — 스테일 호출이 새 점유자를 쫓아내지 못하게 가드.
    if (OccupantWeak.Get() != Occupant) return;

    OccupantWeak.Reset();
    bOccupied = false;
}

void AFurnitureActor::BeginPlay()
{
    Super::BeginPlay();

    // 에디터 미지정 방어 — GUID 폴백(DroppedItemBase 의 TemplateID 폴백과 동일 취지).
    if (FurnitureID.IsEmpty())
    {
        FurnitureID = FString::Printf(TEXT("Furniture_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
    }

    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UFurnitureManager* Manager = GameInstance->GetSubsystem<UFurnitureManager>())
        {
            Manager->RegisterFurniture(FurnitureID, this);
        }
    }
}

void AFurnitureActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 파괴·레벨 언로드 어느 경우에도 매니저 캐시에 댕글링 포인터를 남기지 않는다.
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UFurnitureManager* Manager = GameInstance->GetSubsystem<UFurnitureManager>())
        {
            Manager->UnregisterFurniture(FurnitureID);
        }
    }

    Super::EndPlay(EndPlayReason);
}
