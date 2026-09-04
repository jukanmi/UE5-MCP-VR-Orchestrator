#include "PlayerHUDWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "GameFramework/Pawn.h"
#include "../../Core/Entity.h"               // IPlayerBase / UPlayerBase
#include "../../Inventory/InventoryComponent.h"

void UPlayerHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 소유 폰 캐시 — HP/인벤토리 조회 대상.
    OwnerPawn = GetOwningPlayerPawn();

    // 인벤토리 변경 → 슬롯 UI 자동 갱신
    TryBindInventoryDelegate();

    // 초기 인벤토리 UI 1회 구성.
    RequestInventoryRefresh();

    // 인벤토리 패널은 닫힌 상태로 시작 — WBP 에서 Hidden 지정을 깜빡해도 코드가 확정.
    SetInventoryPanelVisible(false);
}

void UPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // 폰이 늦게 소유되거나 리스폰/빙의 전환으로 교체될 수 있어 매 틱 보정.
    // null 은 무시한다 — 빙의 전환 중 한 프레임만 비는 경우가 있어, 그대로 반영하면
    // 멀쩡한 OwnerPawn 을 지우고 인벤토리 델리게이트까지 끊어 UI 가 영구히 멈춘다.
    APawn* CurrentPawn = GetOwningPlayerPawn();
    if (CurrentPawn && CurrentPawn != OwnerPawn)
    {
        // 구 폰 유효 시 인벤토리 델리게이트 해제 — Unpossess 후 빙의 전환 누수 방지
        if (IsValid(OwnerPawn))
        {
            if (UInventoryComponent* OldInv = OwnerPawn->FindComponentByClass<UInventoryComponent>())
            {
                OldInv->OnInventoryChanged.RemoveDynamic(this, &UPlayerHUDWidget::RequestInventoryRefresh);
            }
        }
        OwnerPawn = CurrentPawn;
        bInventoryDelegateBound = false;
        TryBindInventoryDelegate();
    }

    if (HealthBar)
    {
        HealthBar->SetPercent(GetHealthPercent());
    }
    if (HealthText)
    {
        HealthText->SetText(FText::FromString(
            FString::Printf(TEXT("%.0f / %.0f"), GetCurrentHealth(), GetMaxHealth())));
    }
}

// ============================================================================
// HP — IPlayerBase 경유 (구체 클래스 비의존)
// ============================================================================

float UPlayerHUDWidget::GetCurrentHealth() const
{
    if (OwnerPawn && OwnerPawn->Implements<UPlayerBase>())
    {
        const FPlayerAttributes Attr = IPlayerBase::Execute_GetPlayerAttributes(OwnerPawn);
        return Attr.Resources.Health;
    }
    return 0.f;
}

float UPlayerHUDWidget::GetMaxHealth() const
{
    if (OwnerPawn && OwnerPawn->Implements<UPlayerBase>())
    {
        const FPlayerAttributes Attr = IPlayerBase::Execute_GetPlayerAttributes(OwnerPawn);
        return Attr.Resources.MaxHealth;
    }
    return 0.f;
}

float UPlayerHUDWidget::GetHealthPercent() const
{
    const float Max = GetMaxHealth();
    return Max > KINDA_SMALL_NUMBER ? FMath::Clamp(GetCurrentHealth() / Max, 0.f, 1.f) : 0.f;
}

// ============================================================================
// Inventory — 소유 폰의 컴포넌트 직접 조회 (타입 비의존)
// ============================================================================

UInventoryComponent* UPlayerHUDWidget::GetInventory() const
{
    return OwnerPawn ? OwnerPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
}

TArray<FInventorySlot> UPlayerHUDWidget::GetInventorySlots() const
{
    if (UInventoryComponent* Inv = GetInventory())
    {
        return Inv->InventorySlots;
    }
    return {};
}

void UPlayerHUDWidget::RequestInventoryRefresh()
{
    OnInventoryUpdated();
}

// ============================================================================
// Inventory — 열기/닫기
// ============================================================================

void UPlayerHUDWidget::SetInventoryPanelVisible(bool bVisible)
{
    bInventoryVisible = bVisible;

    if (InventoryPanel)
    {
        // Collapsed — 닫힌 패널이 레이아웃 공간을 먹지 않게(HP 바 밀림 방지).
        InventoryPanel->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    // 닫힌 동안의 변경분을 열 때 한 번에 반영.
    if (bVisible)
    {
        RequestInventoryRefresh();
    }

    OnInventoryPanelVisibilityChanged(bVisible);
}

bool UPlayerHUDWidget::ToggleInventoryVisibility()
{
    SetInventoryPanelVisible(!bInventoryVisible);
    return bInventoryVisible;
}

void UPlayerHUDWidget::TryBindInventoryDelegate()
{
    if (bInventoryDelegateBound) return;

    if (UInventoryComponent* Inv = GetInventory())
    {
        Inv->OnInventoryChanged.AddDynamic(this, &UPlayerHUDWidget::RequestInventoryRefresh);
        bInventoryDelegateBound = true;
        // 바인딩 전 변경분(초기 지급 아이템 등) 반영
        RequestInventoryRefresh();
    }
}
