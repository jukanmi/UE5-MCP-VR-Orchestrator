#include "Story/QuestMarkerActor.h"

#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
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

    // 멀수록 크게 — 크기가 고정이면 먼 곳에선 점이 된다. 카메라 거리에 비례해 높이를 정하고(클램프),
    // 원뿔 기본 높이(100cm)·가로 비율(0.6)에 맞춰 스케일한다. 꼭짓점 높이가 변하지 않게 커진 만큼 반쯤 더 띄운다.
    constexpr float ConeBaseHeight = 100.f;
    float MarkerHeight = MinMarkerHeight;
    if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        MarkerHeight = FMath::Clamp(FVector::Dist(Cam->GetCameraLocation(), Base) * DistanceScale, MinMarkerHeight, MaxMarkerHeight);
    }
    const float ScaleZ = MarkerHeight / ConeBaseHeight;
    Mesh->SetRelativeScale3D(FVector(ScaleZ * 0.6f, ScaleZ * 0.6f, ScaleZ));
    const float Hover = HoverHeight + (MarkerHeight - ConeBaseHeight) * 0.5f;

    SetActorLocation(Base + FVector(0.f, 0.f, Hover + FMath::Sin(T * BobSpeed) * BobAmplitude));
    AddActorWorldRotation(FRotator(0.f, SpinSpeed * DeltaSeconds, 0.f));
}
