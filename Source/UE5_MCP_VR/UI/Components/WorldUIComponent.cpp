#include "UI/Components/WorldUIComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

UWorldUIComponent::UWorldUIComponent()
{
    // 보일 때만 틱(OnVisibilityChanged) — 숨은 위젯은 다시 그릴 것도 정렬할 것도 없다.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    SetWidgetSpace(EWidgetSpace::World);
    SetTwoSided(true);
    // 페이드가 알파를 쓰므로 Masked 로는 안 된다 — Masked 는 알파를 0/1 로 잘라 중간값이 계단식으로 튄다.
    SetBlendMode(EWidgetBlendMode::Transparent);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UWorldUIComponent::BeginPlay()
{
    Super::BeginPlay();
    SetComponentTickEnabled(IsVisible());
}

void UWorldUIComponent::OnVisibilityChanged()
{
    Super::OnVisibilityChanged();
    if (HasBegunPlay()) SetComponentTickEnabled(IsVisible());
}

void UWorldUIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    FaceViewer();
    UpdateUI(DeltaTime);
}

bool UWorldUIComponent::GetViewPoint(FVector& OutLocation, FVector& OutForward) const
{
    const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!Cam) return false;
    OutLocation = Cam->GetCameraLocation();
    OutForward = Cam->GetCameraRotation().Vector();
    return true;
}

void UWorldUIComponent::FaceViewer()
{
    if (Facing == EWorldUIFacing::None) return;
    FVector CamLoc, CamForward;
    if (!GetViewPoint(CamLoc, CamForward)) return;
    const FVector ToCam = CamLoc - GetComponentLocation();
    if (ToCam.IsNearlyZero()) return;

    // +X 를 카메라 쪽으로. Rotation() 은 Roll 0 이라 기울지 않는다.
    FRotator Look = ToCam.Rotation();
    if (Facing == EWorldUIFacing::FaceCameraYaw) Look = FRotator(0.f, Look.Yaw, 0.f);
    SetWorldRotation(Look.Quaternion() * FacingOffset.Quaternion());
}
