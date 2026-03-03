#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemDataAsset.h"
#include "InventoryComponent.generated.h"

/**
 * 인벤토리의 한 슬롯을 정의하는 구조체 (슬롯 번호 없음, 배열 인덱스 기반).
 * - ItemData가 nullptr이거나 Count가 0이면 빈 슬롯으로 간주.
 */
USTRUCT(BlueprintType)
struct FInventorySlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    FItemData ItemData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    int32 Count;

    // 기본 생성자
    FInventorySlot() : Count(0) {}

    // 빈 슬롯 검사 유틸리티
    bool IsEmpty() const { return !ItemData.IsValidItem() || Count <= 0; }
};

// 장비 착용 부위 (Equipment Slots)는 이제 ItemDataAsset.h에 정의되어 있습니다.

/**
 * 범용 인벤토리 컴포넌트 (Generic Inventory Component).
 * - 슬롯(Slot) 기반의 인벤토리 시스템.
 * - 무게(Weight)와 용량(Capacity) 제한 로직 포함.
 * - 장비(Equipment) 시스템 통합.
 * - Player, NPC, Box 등 다양한 액터에서 사용 가능.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    // --- Configuration (Blueprint에서 설정) ---

    // 인벤토리의 공간적 한계를 두어 플레이어 경험/밸런스를 조절하기 위한 설정값입니다.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    int32 MaxSlotCapacity = 10;

    // 최대 무게 제한 (kg 단위, 기본 50kg)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    float MaximumWeightLimit = 50.0f;

    // 튜토리얼이나 기본 지급 장비를 일괄 적용하기 위해 스폰 시점에 미리 가져올 에셋들의 DataTable Row 이름입니다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
    TArray<FString> InitialDefaultItems;

    // --- Runtime Data (Play 중 변화) ---

    // 실제 인벤토리 슬롯 배열 (Blueprint에서 조회 가능)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|State")
    TArray<FInventorySlot> InventorySlots;

    // 장착 중인 아이템 (Equipment Slots)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|State")
    TMap<EEquipmentSlot, FInventorySlot> EquipmentSlots;

    // 현재 총 무게 (kg)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|State")
    float CurrentWeight = 0.0f;


    // --- Public API ---

    UInventoryComponent();

protected:
    // 초기화: DefaultItems 지급
    virtual void BeginPlay() override;

public:
    /**
     * 아이템을 인벤토리에 추가합니다.
     * 1. 기존 스택이 있고(Stackable), 공간이 남았다면 합칩니다.
     * 2. 남은 수량은 빈 슬롯을 찾아 넣습니다.
     * 3. 공간 부족 혹은 무게 초과 시 실패(false)를 반환합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
    bool AddItem(const FItemData& Item, int32 Amount = 1, bool bCheckWeight = true);

    /**
     * 아이템을 인벤토리에서 제거합니다. (소비, 버리기, 거래)
     * - ItemID 기반으로 검색하여 수량을 차감합니다.
     * - 성공 시 true, 수량 부족 시 false 반환.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
    bool RemoveItem(const FString& ItemID, int32 Amount = 1);

    /**
     * 특정 아이템을 일정 수량 이상 가지고 있는지 확인합니다.
     */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    bool HasItem(const FString& ItemID, int32 Amount = 1);

    /**
     * 아이템 수리
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
    bool RepairItem(const FString& ItemID, float Amount);
    // --- Equipment API ---

    /**
     * 특정 아이템을 지정된 슬롯에 장착합니다.
     * @param ItemID 장착할 아이템 ID
     * @param Slot 장착할 부위 (아이템 데이터에 슬롯 정보가 없다면 수동 지정)
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Equipment")
    bool EquipItem(const FString& ItemID, EEquipmentSlot Slot = EEquipmentSlot::None);

    /**
     * 지정된 슬롯의 장비를 해제하여 인벤토리로 되돌립니다.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Equipment")
    bool UnequipItem(EEquipmentSlot Slot);

    /**
     * (Overload) 아이템 ID를 기반으로 장비를 해제합니다.
     * - 해당 아이템이 장착된 슬롯을 찾아 해제합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Equipment")
    bool UnequipItemByID(const FString& ItemID);

    /**
     * 해당 슬롯에 장착된 아이템 정보를 반환합니다. (없으면 nullptr)
     */
    UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
    FInventorySlot GetEquippedItem(EEquipmentSlot Slot) const;

    // 현재 무게 다시 계산 (디버그/검증용)
    UFUNCTION(BlueprintCallable, Category = "Inventory|Utils")
    void CalculateWeight();

protected:
    // 기존 인벤토리에 같은 아이템이 있다면 잔여 공간(MaxStack)만큼 채워넣어 슬롯 낭비를 방지합니다.
    int32 TryStackItemsExisting(const FItemData& TargetItem, int32 RemainingAmount);

    // 합치고도 남은 아이템들을 빈 슬롯들을 찾아 분산 배치하여 꽉 찰 때까지 저장합니다.
    int32 TryStoreInEmptySlots(const FItemData& TargetItem, int32 RemainingAmount);

    // 내부 유틸: 빈 슬롯 찾기
    int32 GetFirstAvailableSlotIndex() const;
    
    // 내부 유틸: 해당 아이템이 들어있는(스택 가능한) 제일 빠른 슬롯 찾기
    int32 GetStackableSlotIndex(const FItemData& TargetItem) const;

    // 내부 유틸: 인벤토리 내 아이템 검색 (인덱스 반환, 실패 시 -1)
    int32 GetSlotIndexByItemID(const FString& InItemID) const;
};
