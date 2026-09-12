// PlayerHUDWidget — 플레이어 HP·스태미나 바 + 인벤토리 슬롯 표시 HUD.
//
// 구체 플레이어 클래스(AVRPawn 등)에 의존하지 않는다:
//   - HP/스태미나 : 소유 폰의 IPlayerBase::GetPlayerAttributes
//   - 인벤토리: 소유 폰의 UInventoryComponent (FindComponentByClass)
// 덕분에 IPlayerBase 를 구현한 폰이면 어느 것이든 동일 위젯으로 동작.
//
// WBP 사용법:
//   - 이 클래스를 부모로 하는 WBP 작성.
//   - (선택) "HealthBar"/"StaminaBar"(UProgressBar) · "HealthText"/"StaminaText"(UTextBlock)
//     이름 위젯 배치 → 자동 바인딩.
//   - 인벤토리 슬롯 UI 는 OnInventoryUpdated 이벤트에서 GetInventorySlots() 로 재구성.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/Components/InventoryComponent.h" // FInventorySlot (UFUNCTION 반환 타입 노출)
#include "PlayerHUDWidget.generated.h"

class UProgressBar;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UBorder;
class UPanelWidget;
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

    // --- Stamina ---

    /** 스태미나 진행 바 — WBP 에 같은 이름 UProgressBar 배치 시 자동 바인딩(선택). */
    UPROPERTY(meta = (BindWidgetOptional))
    UProgressBar* StaminaBar;

    /** 스태미나 수치 텍스트 — WBP 에 같은 이름 UTextBlock 배치 시 자동 바인딩(선택). */
    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* StaminaText;

    // --- Inventory ---

    /** 소유 플레이어의 인벤토리 컴포넌트(없으면 nullptr). */
    UFUNCTION(BlueprintPure, Category = "HUD|Inventory")
    UInventoryComponent* GetInventory() const;

    /** 현재 인벤토리 슬롯 스냅샷 — WBP 가 슬롯 위젯 재구성에 사용. */
    UFUNCTION(BlueprintPure, Category = "HUD|Inventory")
    TArray<FInventorySlot> GetInventorySlots() const;

    /** 특정 장비 슬롯에 장착된 아이템 정보 반환. */
    UFUNCTION(BlueprintPure, Category = "HUD|Inventory")
    FInventorySlot GetEquippedSlotItem(EEquipmentSlot EquipSlot) const;

    /** 장비 슬롯의 아이템 해제 — 인벤토리로 되돌리고 비주얼 메시 파괴. */
    UFUNCTION(BlueprintCallable, Category = "HUD|Inventory")
    bool UnequipSlot(EEquipmentSlot EquipSlot);

    /** 인벤토리 변경 알림 — WBP 가 슬롯 UI 를 다시 그림. 획득/소비 후 RequestInventoryRefresh 호출. */
    UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Inventory")
    void OnInventoryUpdated();

    /** 인벤토리 UI 갱신 요청 — OnInventoryUpdated 브로드캐스트 후 선택 슬롯을 다시 강조한다. */
    UFUNCTION(BlueprintCallable, Category = "HUD|Inventory")
    void RequestInventoryRefresh();

    /**
     * 선택 슬롯 강조 — 슬롯 그리드의 자식을 순회하며 선택된 것만 색을 바꾼다.
     * WBP 그래프를 건드리지 않으려고 C++ 에서 위젯 트리를 직접 훑는다. 그리드 자식 순서가
     * 곧 슬롯 인덱스라는 전제(WBP 가 Index 순으로 Add 함)에 기댄다.
     */
    UFUNCTION(BlueprintCallable, Category = "HUD|Inventory")
    void ApplySelectionHighlight();

    /** 슬롯 그리드 위젯 이름 — WBP 에서 이름이 바뀌면 여기만 고친다. */
    UPROPERTY(EditDefaultsOnly, Category = "HUD|Inventory")
    FName SlotGridName = TEXT("SlotGrid");

    /** 선택된 슬롯 테두리 색. */
    UPROPERTY(EditDefaultsOnly, Category = "HUD|Inventory")
    FLinearColor SelectedSlotColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

    /** 비선택 슬롯 테두리 색(원래 색으로 되돌릴 때 쓴다). */
    UPROPERTY(EditDefaultsOnly, Category = "HUD|Inventory")
    FLinearColor NormalSlotColor = FLinearColor(1.f, 1.f, 1.f, 0.25f);

    /** 테두리(UBorder)가 없는 슬롯 위젯을 위한 대체 강조 — 선택된 칸만 이만큼 커진다. */
    UPROPERTY(EditDefaultsOnly, Category = "HUD|Inventory", meta = (ClampMin = "1.0", ClampMax = "1.5"))
    float SelectedSlotScale = 1.12f;

    // --- Chat ---
    /** NPC 채팅 입력 — WBP 에 "ChatInput" 이름 UEditableTextBox 배치 시 자동 바인딩(선택).
     *  Enter 로 커밋하면 소유 폰 근처 최근접 NPC 에게 전송하고 칸을 비운다. */
    UPROPERTY(meta = (BindWidgetOptional))
    UEditableTextBox* ChatInput;

    /** 채팅 기록 — WBP 에 "ChatLog" 이름 UScrollBox 배치 시 자동 바인딩(선택).
     *  보낸 메시지와 NPCManager 의 NPC 응답을 한 줄씩 쌓고 오래된 줄부터 버린다. */
    UPROPERTY(meta = (BindWidgetOptional))
    UScrollBox* ChatLog;

    /** ChatLog 최대 줄 수 — 넘치면 맨 위부터 제거. */
    UPROPERTY(EditDefaultsOnly, Category = "HUD|Chat", meta = (ClampMin = "1"))
    int32 ChatLogMaxLines = 8;

    /** ChatLog 글자 크기. */
    UPROPERTY(EditDefaultsOnly, Category = "HUD|Chat")
    int32 ChatLogFontSize = 12;

    /** ChatLog 에 "화자: 내용" 한 줄 추가. ChatLog 없으면 무시. */
    UFUNCTION(BlueprintCallable, Category = "HUD|Chat")
    void AppendChatLine(const FString& Speaker, const FString& Text);

    /** 채팅 칸에 키보드 포커스 — 폰이 Enter 키에서 호출. ChatInput 없으면 false. */
    UFUNCTION(BlueprintCallable, Category = "HUD|Chat")
    bool FocusChatInput();

    /** 채팅 입력 중인지 — 폰이 시선 페이드 억제·이동 차단 판단에 쓴다. */
    UFUNCTION(BlueprintPure, Category = "HUD|Chat")
    bool IsChatFocused() const;

    /** 채팅 대상 탐색 반경(cm) — 폰의 A버튼 NPC 탐지와 같은 값. */
    UPROPERTY(EditDefaultsOnly, Category = "HUD|Chat")
    float ChatTargetRadius = 500.f;

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

    /** ChatInput 커밋 — Enter 만 처리: 텍스트 있으면 전송, 있든 없든 포커스를 게임으로 돌려 닫는다. */
    UFUNCTION()
    void HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod);

    /** NPCManager::OnNPCResponseReceived → ChatLog 한 줄. */
    UFUNCTION()
    void HandleNPCResponse(const FString& NPCName, const FString& Message);

    virtual void NativeDestruct() override;

    /** 인벤토리 OnInventoryChanged 델리게이트 바인딩 시도 — 1회 성공 시 재시도 안 함.
     *  폰이 늦게 잡히는 경우 대비 NativeTick 에서도 호출. */
    void TryBindInventoryDelegate();

    bool bInventoryDelegateBound = false;

    /** 직전 프레임 HP — 값이 그대로면 위젯을 건드리지 않는다. -1 은 강제 갱신 표시. */
    float CachedHealth = -1.f;
    float CachedMaxHealth = -1.f;

    /** 직전 프레임 스태미나. 바는 이 값 비교로, 텍스트는 정수부 비교로 갱신을 거른다. */
    float CachedStamina = -1.f;
    float CachedMaxStamina = -1.f;

    /** 인벤토리 패널 표시 상태 — 시작은 닫힘. */
    UPROPERTY(BlueprintReadOnly, Category = "HUD|Inventory")
    bool bInventoryVisible = false;
};
