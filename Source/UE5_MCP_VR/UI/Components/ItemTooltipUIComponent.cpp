#include "UI/Components/ItemTooltipUIComponent.h"

#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Subsystems/ItemManager.h"
#include "UI/BP/ItemTooltipWidget.h"

UItemTooltipUIComponent::UItemTooltipUIComponent()
{
    Facing = EWorldUIFacing::FaceCamera;
    SetDrawSize(FVector2D(400.f, 140.f));
    SetRelativeScale3D(FVector(0.05f));
    SetWidgetClass(UItemTooltipWidget::StaticClass());
    SetVisibility(false);
}

void UItemTooltipUIComponent::SetItem(const FItemData& Data, int32 Amount, int32 Price)
{
    if (UItemTooltipWidget* W = GetWidgetAs<UItemTooltipWidget>())
    {
        W->SetItem(Data, Amount, Price);
    }
}

void UItemTooltipUIComponent::ShowForItem(ADroppedItemBase* Item)
{
    if (!Item)
    {
        Clear();
        return;
    }

    if (Item != Target)
    {
        Target = Item;
        UItemManager* ItemManager = UItemManager::Get(this);
        FItemData Data;
        if (!ItemManager || !ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, Data))
        {
            // 대상을 기억하지 않는다 — 기억하면 다음 틱에 같은 대상이라 조회를 건너뛰고 빈 이름표를 띄운다.
            Clear();
            return;
        }
        SetItem(Data, Item->Amount, Item->bIsDisplayed ? Item->DisplayPrice : -1);
    }

    SetWorldLocation(Item->GetActorLocation() + FVector(0.f, 0.f, HeightOffset));
    if (!IsVisible()) ShowUI();
}

void UItemTooltipUIComponent::Clear()
{
    if (IsVisible()) HideUI();
    Target = nullptr;
}
