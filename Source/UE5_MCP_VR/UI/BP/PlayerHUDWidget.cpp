#include "UI/BP/PlayerHUDWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerController.h"
#include "Components/ScrollBox.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Components/Widget.h"
#include "GameFramework/Pawn.h"
#include "Core/Interfaces/Entity.h"               // IPlayerBase / UPlayerBase
#include "Inventory/Components/InventoryComponent.h"
#include "Core/Utils/PlayerInteractionUtils.h"
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

    // 채팅 입력 — WBP 에 ChatInput 이 있을 때만. 위젯 재구성 시 중복 바인딩 방지로 Unique.
    if (ChatInput)
    {
        ChatInput->OnTextCommitted.AddUniqueDynamic(this, &UPlayerHUDWidget::HandleChatCommitted);
    }
    // NPC 응답 → 채팅 기록. GameInstance 서브시스템이라 위젯보다 오래 살므로 Destruct 에서 해제.
    if (ChatLog)
    {
        if (UNPCManager* Manager = UNPCManager::Get(this))
        {
            Manager->OnNPCResponseReceived.AddUniqueDynamic(this, &UPlayerHUDWidget::HandleNPCResponse);
        }
    }
}

void UPlayerHUDWidget::NativeDestruct()
{
    if (UNPCManager* Manager = UNPCManager::Get(this))
    {
        Manager->OnNPCResponseReceived.RemoveDynamic(this, &UPlayerHUDWidget::HandleNPCResponse);
    }
    Super::NativeDestruct();
}

void UPlayerHUDWidget::AppendChatLine(const FString& Speaker, const FString& Text)
{
    if (!ChatLog || !WidgetTree) return;

    UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Line->SetText(FText::FromString(FString::Printf(TEXT("%s: %s"), *Speaker, *Text)));
    Line->SetAutoWrapText(true);
    FSlateFontInfo Font = Line->GetFont();
    Font.Size = ChatLogFontSize;
    Line->SetFont(Font);
    ChatLog->AddChild(Line);

    while (ChatLog->GetChildrenCount() > ChatLogMaxLines)
    {
        ChatLog->RemoveChildAt(0);
    }
    ChatLog->ScrollToEnd();
}

void UPlayerHUDWidget::HandleNPCResponse(const FString& NPCName, const FString& Message)
{
    AppendChatLine(NPCName, Message);
}

bool UPlayerHUDWidget::FocusChatInput()
{
    if (!ChatInput) return false;
    ChatInput->SetKeyboardFocus();
    return true;
}

bool UPlayerHUDWidget::IsChatFocused() const
{
    return ChatInput && ChatInput->HasKeyboardFocus();
}

void UPlayerHUDWidget::HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod != ETextCommit::OnEnter) return;

    const FString Msg = Text.ToString().TrimStartAndEnd();
    if (!Msg.IsEmpty() && OwnerPawn)
    {
        const FString Target = PlayerInteractionUtils::FindNearestNPCId(OwnerPawn, ChatTargetRadius);
        if (Target.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("[HUD] 채팅 폐기 — 반경 %.0fcm 내 NPC 없음"), ChatTargetRadius);
        }
        else
        {
            PlayerInteractionUtils::SendDialogueToNpc(this, OwnerPawn->GetName(), Target, Msg);
            AppendChatLine(TEXT("나"), Msg);
        }
    }

    // 빈 Enter 는 닫기, 전송 후에도 닫기 — 포커스가 남아 있으면 WASD 가 글자로 들어간다.
    // SetFocusToGameViewport 는 Slate 포커스만 옮기고 뷰포트 입력 캡처를 안 돌려줘 이동이 죽는다 —
    // 입력 모드 GameOnly 가 포커스·캡처를 함께 복구한다.
    ChatInput->SetText(FText::GetEmpty());
    if (APlayerController* PC = GetOwningPlayer())
    {
        PC->SetInputMode(FInputModeGameOnly());
    }
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
