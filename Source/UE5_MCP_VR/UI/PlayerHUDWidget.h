// PlayerHUDWidget — 플레이어 HP 바 + 인벤토리 슬롯 표시 HUD.
//
// 구체 플레이어 클래스(AVRPlayerCharacter / AVRPawn)에 의존하지 않는다:
//   - HP   : 소유 폰의 IPlayerBase::GetPlayerAttributes (양 클래스 모두 구현)
//   - 인벤토리: 소유 폰의 UInventoryComponent (FindComponentByClass)
// 덕분에 두 플레이어 타입 모두 동일 위젯으로 동작.
//
// WBP 사용법:
//   - 이 클래스를 부모로 하는 WBP 작성.
//   - (선택) "HealthBar"(UProgressBar) / "HealthText"(UTextBlock) 이름 위젯 배치 → 자동 바인딩.
//   - 인벤토리 슬롯 UI 는 OnInventoryUpdated 이벤트에서 GetInventorySlots() 로 재구성.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "../Inventory/InventoryComponent.h" // FInventorySlot (UFUNCTION 반환 타입 노출)
#include "PlayerHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
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

protected:
    /** 소유 폰 캐시 (NativeConstruct 에서 1회 해석). */
    UPROPERTY(BlueprintReadOnly, Category = "HUD")
    TObjectPtr<APawn> OwnerPawn;

    /** 인벤토리 OnInventoryChanged 델리게이트 바인딩 시도 — 1회 성공 시 재시도 안 함.
     *  폰이 늦게 잡히는 경우 대비 NativeTick 에서도 호출. */
    void TryBindInventoryDelegate();

    bool bInventoryDelegateBound = false;
};
