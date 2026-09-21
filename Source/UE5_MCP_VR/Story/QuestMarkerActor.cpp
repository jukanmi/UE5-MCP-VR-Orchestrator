#include "Story/QuestMarkerActor.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AQuestMarkerActor::AQuestMarkerActor()
{
    PrimaryActorTick.bCanEverTick = true;

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    // 엔진 기본 원뿔(높이 100)을 뒤집어 꼭짓점이 대상을 가리키게. 무광 단색 머티리얼은 조명(저녁)과 무관하게 보인다.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Unlit(TEXT("/Engine/EngineDebugMaterials/LevelColorationUnlitMaterial.LevelColorationUnlitMaterial"));
    if (Cone.Succeeded()) Mesh->SetStaticMesh(Cone.Object);
    if (Unlit.Succeeded()) Mesh->SetMaterial(0, Unlit.Object);
    Mesh->SetRelativeRotation(FRotator(180.f, 0.f, 0.f));
    Mesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.0f));

    SetActorHiddenInGame(true);
}

void AQuestMarkerActor::BeginPlay()
{
    Super::BeginPlay();
    if (UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
    {
        MID->SetVectorParameterValue(TEXT("Color"), Color);
    }
}

void AQuestMarkerActor::SetTarget(AActor* InTarget)
{
    Target = InTarget;
    SetActorHiddenInGame(!Target.IsValid());
    if (Target.IsValid())
    {
        SetActorLocation(Target->GetActorLocation() + FVector(0.f, 0.f, HoverHeight));
    }
}

void AQuestMarkerActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Target.IsValid())
    {
        if (!IsHidden()) SetActorHiddenInGame(true);  // 대상 파괴(보스 사망 등)
        return;
    }
    const float T = GetWorld()->GetTimeSeconds();
    const FVector Base = Target->GetActorLocation();
    SetActorLocation(Base + FVector(0.f, 0.f, HoverHeight + FMath::Sin(T * BobSpeed) * BobAmplitude));
    AddActorWorldRotation(FRotator(0.f, SpinSpeed * DeltaSeconds, 0.f));
}
