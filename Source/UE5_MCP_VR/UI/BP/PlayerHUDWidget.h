// PlayerHUDWidget — 플레이어 HP 바 + 인벤토리 슬롯 표시 HUD.
//
// 구체 플레이어 클래스(AVRPawn 등)에 의존하지 않는다:
//   - HP   : 소유 폰의 IPlayerBase::GetPlayerAttributes
//   - 인벤토리: 소유 폰의 UInventoryComponent (FindComponentByClass)
// 덕분에 IPlayerBase 를 구현한 폰이면 어느 것이든 동일 위젯으로 동작.
//
// WBP 사용법:
//   - 이 클래스를 부모로 하는 WBP 작성.
//   - (선택) "HealthBar"(UProgressBar) / "HealthText"(UTextBlock) 이름 위젯 배치 → 자동 바인딩.
//   - 인벤토리 슬롯 UI 는 OnInventoryUpdated 이벤트에서 GetInventorySlots() 로 재구성.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "../../Inventory/InventoryComponent.h" // FInventorySlot (UFUNCTION 반환 타입 노출)
#include "PlayerHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UWidget;
class UInventoryComponent;

UCLASS()
class UE5_MCP_VR_API UPlayerHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** HP 진행 바 — WBP 에 같은 이름 UProgressBar 배치 시 자동 바인딩(선택). */
    UPROPERTY(meta = (BindWidgetOptional))
    UProgressBar* HealthBar;

    /** HP 수치 텍스트 — WBP 에 같은 이름 UTextBlock 배치 시 자동 바인딩(선택). */
    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* HealthText;

    // --- HP ---

    /** 현재 HP 비율 0~1 (MaxHealth 0 이면 0). */
    UFUNCTION(BlueprintPure, Category = "HUD|Health")
    float GetHealthPercent() const;

    UFUNCTION(BlueprintPure, Category = "HUD|Health")
    float GetCurrentHealth() const;

    UFUNCTION(BlueprintPure, Category = "HUD|Health")
    float GetMaxHealth() const;

    // --- Inventory ---

    /** 소유 플레이어의 인벤토리 컴포넌트(없으면 nullptr). */
    UFUNCTION(BlueprintPure, Category = "HUD|Inventory")
    UInventoryComponent* GetInventory() const;

    /** 현재 인벤토리 슬롯 스냅샷 — WBP 가 슬롯 위젯 재구성에 사용. */
    UFUNCTION(BlueprintPure, Category = "HUD|Inventory")
    TArray<FInventorySlot> GetInventorySlots() const;

    /** 인벤토리 변경 알림 — WBP 가 슬롯 UI 를 다시 그림. 획득/소비 후 RequestInventoryRefresh 호출. */
    UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Inventory")
    void OnInventoryUpdated();

    /** 인벤토리 UI 갱신 요청 — OnInventoryUpdated 브로드캐스트. */
    UFUNCTION(BlueprintCallable, Category = "HUD|Inventory")
    void RequestInventoryRefresh();

    // --- Inventory 열기/닫기 ---

    /** 인벤토리 패널 — WBP 에 "InventoryPanel" 이름 위젯 배치 시 자동 바인딩(선택).
     *  바인딩되면 C++ 가 Visible/Collapsed 를 직접 토글하므로 WBP 구현이 불필요. */
    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* InventoryPanel;

    /** 현재 인벤토리 패널이 열려 있는지. */
    UFUNCTION(BlueprintPure, Category = "HUD|Inventory")
    bool IsInventoryVisible() const { return bInventoryVisible; }

    /** 인벤토리 패널 표시 상태 지정. 열 때 슬롯 UI 를 1회 갱신한다. */
    UFUNCTION(BlueprintCallable, Category = "HUD|Inventory")
    void SetInventoryPanelVisible(bool bVisible);

    /** 열림/닫힘 토글 — 토글 후의 상태를 반환. */
    UFUNCTION(BlueprintCallable, Category = "HUD|Inventory")
    bool ToggleInventoryVisibility();

    /** 표시 상태 변경 알림 — InventoryPanel 미바인딩 시 WBP 가 직접 패널을 처리. */
    UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Inventory")
    void OnInventoryPanelVisibilityChanged(bool bVisible);

protected:
    /** 소유 폰 캐시 (NativeConstruct 에서 1회 해석). */
    UPROPERTY(BlueprintReadOnly, Category = "HUD")
    TObjectPtr<APawn> OwnerPawn;

    /** 인벤토리 OnInventoryChanged 델리게이트 바인딩 시도 — 1회 성공 시 재시도 안 함.
     *  폰이 늦게 잡히는 경우 대비 NativeTick 에서도 호출. */
    void TryBindInventoryDelegate();

    bool bInventoryDelegateBound = false;

    /** 인벤토리 패널 표시 상태 — 시작은 닫힘. */
    UPROPERTY(BlueprintReadOnly, Category = "HUD|Inventory")
    bool bInventoryVisible = false;
};
