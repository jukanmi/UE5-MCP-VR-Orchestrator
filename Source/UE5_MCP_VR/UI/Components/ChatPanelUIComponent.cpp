#include "UI/Components/ChatPanelUIComponent.h"

#include "UI/BP/ChatWidget.h"

UChatPanelUIComponent::UChatPanelUIComponent()
{
    Facing = EWorldUIFacing::None;   // 카메라 부착 — 늘 정면이라 정렬이 필요 없다
    SetVisibility(false);            // Enter 전까지 숨김
}

void UChatPanelUIComponent::Open()
{
    UChatWidget* Chat = Cast<UChatWidget>(GetUserWidgetObject());
    if (!Chat) return;
    ShowUI();
    Chat->FocusChatInput();
}

void UChatPanelUIComponent::UpdateUI(float /*DeltaTime*/)
{
    const UChatWidget* Chat = Cast<UChatWidget>(GetUserWidgetObject());
    if (Chat && !Chat->IsChatFocused()) HideUI();
}
