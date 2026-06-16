#include "PlayerHUDWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "../Core/Entity.h"               // IPlayerBase / UPlayerBase
#include "../Inventory/InventoryComponent.h"

void UPlayerHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 소유 폰 캐시 — HP/인벤토리 조회 대상.
    OwnerPawn = GetOwningPlayerPawn();

    // 인벤토리 변경 → 슬롯 UI 자동 갱신
    TryBindInventoryDelegate();

    // 초기 인벤토리 UI 1회 구성.
    RequestInventoryRefresh();
}

void UPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // 폰이 늦게 소유되거나 리스폰/빙의 전환으로 교체될 수 있어 매 틱 보정.
    APawn* CurrentPawn = GetOwningPlayerPawn();
    if (CurrentPawn != OwnerPawn)
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
