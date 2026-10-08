#include "UI/Components/MenuPanelUIComponent.h"

UMenuPanelUIComponent::UMenuPanelUIComponent()
{
    Facing = EWorldUIFacing::FaceCamera;

    // 월드 고정 — 부모(폰)가 움직여도 열린 자리에 남는다. 위치는 Open 에서 월드 좌표로 직접 넣는다.
    SetUsingAbsoluteLocation(true);
    SetUsingAbsoluteRotation(true);

    // 오른손 포인터(WidgetInteraction, World 모드)가 물리 레이로 위젯을 찾는다 — 손 패널과 같은 이유로 Visibility 만 막는다.
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetCollisionResponseToAllChannels(ECR_Ignore);
    SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

    SetVisibility(false);   // 닫힌 상태로 시작
}

void UMenuPanelUIComponent::Open()
{
    FVector CamLoc, CamForward;
    if (GetViewPoint(CamLoc, CamForward))
    {
        // 정면은 수평만 — 고개를 숙이거나 든 채로 열어도 패널이 비스듬히 놓이지 않는다.
        FVector Flat = CamForward;
        Flat.Z = 0.f;
        Flat = Flat.GetSafeNormal();
        if (Flat.IsNearlyZero()) Flat = CamForward;

        SetWorldLocation(CamLoc + Flat * OpenDistance + FVector(0.f, 0.f, OpenHeightOffset));
    }
    ShowUI();
}
