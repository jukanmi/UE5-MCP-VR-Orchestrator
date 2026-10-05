#include "UI/Components/HUDPanelUIComponent.h"

UHUDPanelUIComponent::UHUDPanelUIComponent()
{
    Facing = EWorldUIFacing::FaceCamera;   // 손목을 어떻게 돌려도 정면으로 읽힌다

    // 오른손 포인터(WidgetInteraction, World 모드)가 물리 레이로 위젯을 찾는다. 충돌을 끄면 광선이 패널을 관통해
    // 클릭·호버가 전달되지 않는다. 이동·물리에는 관여하지 않도록 QueryOnly 로 두고 Visibility 채널만 막는다.
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetCollisionResponseToAllChannels(ECR_Ignore);
    SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

    // 상시 표시 — HP·스태미나 게이지가 실려 있어 인벤토리와 수명이 다르다. 인벤토리 슬롯만 위젯 안에서 접힌다.
    SetVisibility(true);
}

void UHUDPanelUIComponent::UpdateUI(float DeltaTime)
{
    float Target = 1.f;
    FVector CamLoc, CamForward;
    if (!bForceOpaque && GetViewPoint(CamLoc, CamForward))
    {
        const FVector ToPanel = (GetComponentLocation() - CamLoc).GetSafeNormal();
        Target = FVector::DotProduct(CamForward, ToPanel) >= GazeDotThreshold ? 1.f : 0.f;
    }

    // 목표값을 그대로 쓰면 임계 경계에서 손 떨림만으로 깜빡인다 — 보간이 히스테리시스 역할.
    Opacity = FMath::FInterpTo(Opacity, Target, DeltaTime, GazeFadeSpeed);
    SetTintColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Opacity));
}
