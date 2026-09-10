#include "UI/BP/PlayerHUDWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "GameFramework/Pawn.h"
#include "Core/Interfaces/Entity.h"               // IPlayerBase / UPlayerBase
#include "Inventory/Components/InventoryComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/PanelWidget.h"

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
        CachedHealth = -1.f;   // 폰이 바뀌었으니 다음 비교는 무조건 갱신
        CachedStamina = -1.f;
    }

    // HP 는 대개 프레임 간 변하지 않는다. 매 틱 SetText 하면 폰트 셰이핑이 다시 돌아
    // VR 90Hz 에서 그대로 프레임을 갉아먹는다. 속성 조회도 틱당 1회로 줄인다.
    if (IsValid(OwnerPawn) && OwnerPawn->Implements<UPlayerBase>())
    {
        const FPlayerAttributes Attr = IPlayerBase::Execute_GetPlayerAttributes(OwnerPawn);
        const float CurrentHP = Attr.Resources.Health;
        const float MaxHP     = Attr.Resources.MaxHealth;

        if (!FMath::IsNearlyEqual(CurrentHP, CachedHealth) || !FMath::IsNearlyEqual(MaxHP, CachedMaxHealth))
        {
            CachedHealth = CurrentHP;
            CachedMaxHealth = MaxHP;

            if (HealthBar)
            {
                HealthBar->SetPercent(MaxHP > KINDA_SMALL_NUMBER ? FMath::Clamp(CurrentHP / MaxHP, 0.f, 1.f) : 0.f);
            }
            if (HealthText)
            {
                HealthText->SetText(FText::FromString(
                    FString::Printf(TEXT("%.0f / %.0f"), CurrentHP, MaxHP)));
            }
        }

        // 스태미나는 HP 와 달리 Sprint 중 매 프레임 연속으로 변한다. 바는 그대로 반영하되
        // 텍스트는 정수부가 바뀔 때만 갱신한다 — 매 틱 SetText 는 폰트 셰이핑을 다시 돌려
        // VR 90Hz 에서 프레임을 갉아먹는데, 소수점은 어차피 표시하지도 않는다.
        const float CurrentSP = Attr.Resources.Stamina;
        const float MaxSP     = Attr.Resources.MaxStamina;

        if (!FMath::IsNearlyEqual(CurrentSP, CachedStamina) || !FMath::IsNearlyEqual(MaxSP, CachedMaxStamina))
        {
            const bool bTextChanged =
                FMath::FloorToInt(CurrentSP) != FMath::FloorToInt(CachedStamina) ||
                !FMath::IsNearlyEqual(MaxSP, CachedMaxStamina);

            CachedStamina = CurrentSP;
            CachedMaxStamina = MaxSP;

            if (StaminaBar)
            {
                StaminaBar->SetPercent(MaxSP > KINDA_SMALL_NUMBER ? FMath::Clamp(CurrentSP / MaxSP, 0.f, 1.f) : 0.f);
            }
            if (StaminaText && bTextChanged)
            {
                StaminaText->SetText(FText::FromString(
                    FString::Printf(TEXT("%.0f / %.0f"), CurrentSP, MaxSP)));
            }
        }
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
// 스태미나 — HP 와 동일하게 IPlayerBase 경유
// ============================================================================

float UPlayerHUDWidget::GetCurrentStamina() const
{
    if (OwnerPawn && OwnerPawn->Implements<UPlayerBase>())
    {
        const FPlayerAttributes Attr = IPlayerBase::Execute_GetPlayerAttributes(OwnerPawn);
        return Attr.Resources.Stamina;
    }
    return 0.f;
}

float UPlayerHUDWidget::GetMaxStamina() const
{
    if (OwnerPawn && OwnerPawn->Implements<UPlayerBase>())
    {
        const FPlayerAttributes Attr = IPlayerBase::Execute_GetPlayerAttributes(OwnerPawn);
        return Attr.Resources.MaxStamina;
    }
    return 0.f;
}

float UPlayerHUDWidget::GetStaminaPercent() const
{
    const float Max = GetMaxStamina();
    return Max > KINDA_SMALL_NUMBER ? FMath::Clamp(GetCurrentStamina() / Max, 0.f, 1.f) : 0.f;
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

FInventorySlot UPlayerHUDWidget::GetEquippedSlotItem(EEquipmentSlot EquipSlot) const
{
    if (UInventoryComponent* Inv = GetInventory())
    {
        return Inv->GetEquippedItem(EquipSlot);
    }
    return FInventorySlot();
}

bool UPlayerHUDWidget::UnequipSlot(EEquipmentSlot EquipSlot)
{
    if (UInventoryComponent* Inv = GetInventory())
    {
        return Inv->UnequipItem(EquipSlot);
    }
    return false;
}

void UPlayerHUDWidget::RequestInventoryRefresh()
{
    OnInventoryUpdated();

    // 강조는 반드시 BP 갱신 뒤에 — WBP 가 슬롯 위젯을 매번 지우고 새로 만들기 때문에
    // 먼저 칠하면 그 위젯이 통째로 버려진다.
    ApplySelectionHighlight();
}

void UPlayerHUDWidget::ApplySelectionHighlight()
{
    UInventoryComponent* Inv = GetInventory();
    if (!Inv || !WidgetTree) return;

    UPanelWidget* Grid = Cast<UPanelWidget>(WidgetTree->FindWidget(SlotGridName));
    if (!Grid) return;

    const int32 Selected = Inv->SelectedSlotIndex;

    for (int32 i = 0; i < Grid->GetChildrenCount(); ++i)
    {
        UUserWidget* SlotWidget = Cast<UUserWidget>(Grid->GetChildAt(i));
        if (!SlotWidget) continue;

        const bool bIsSelected = (i == Selected);

        // 테두리 색이 1순위. 슬롯 위젯 안의 첫 UBorder 를 쓴다 — 이름에 기대면 WBP 에서
        // 한 번 개명하는 순간 조용히 강조가 사라진다.
        UBorder* Frame = nullptr;
        if (SlotWidget->WidgetTree)
        {
            SlotWidget->WidgetTree->ForEachWidget([&Frame](UWidget* W)
            {
                if (!Frame)
                {
                    Frame = Cast<UBorder>(W);
                }
            });
        }

        if (Frame)
        {
            Frame->SetBrushColor(bIsSelected ? SelectedSlotColor : NormalSlotColor);
        }

        // 테두리가 없는 슬롯 디자인에서도 뭐가 골라졌는지는 보여야 한다 — 크기로 대신한다.
        SlotWidget->SetRenderScale(bIsSelected ? FVector2D(SelectedSlotScale) : FVector2D(1.f));
    }
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
