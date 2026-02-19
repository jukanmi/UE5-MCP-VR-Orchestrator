#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h" // UActorComponent
#include "../Items/ItemDataAsset.h" // UItemDataAsset
#include "NPCInventoryComponent.generated.h" // Must be last

/**
 * 인벤토리의 한 슬롯을 정의하는 구조체 (슬롯 번호 없음, 배열 인덱스 기반).
 * - ItemData가 nullptr이거나 Count가 0이면 빈 슬롯으로 간주.
 */
USTRUCT(BlueprintType)
struct FInventorySlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    UItemDataAsset* ItemData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    int32 Count;

    // 기본 생성자
    FInventorySlot() : ItemData(nullptr), Count(0) {}

    // 빈 슬롯 검사 유틸리티
    bool IsEmpty() const { return ItemData == nullptr || Count <= 0; }
};

/**
 * NPC가 소지한 아이템을 관리하는 컴포넌트 (Option 2 구현).
 * - 슬롯(Slot) 기반의 인벤토리 시스템.
 * - 무게(Weight)와 용량(Capacity) 제한 로직 포함.
 * - LLM에게 현재 인벤토리 상태를 JSON 형태로 전달하는 기능 제공.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    // --- Configuration (Blueprint에서 설정) ---

    // 최대 슬롯 개수 (기본 10)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    int32 MaxSlots = 10;

    // 최대 무게 제한 (kg 단위, 기본 50kg)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    float MaxWeight = 50.0f;

    // NPC 스폰 시 기본 지급될 아이템 목록 (Data Asset 참조)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
    TArray<TSoftObjectPtr<UItemDataAsset>> DefaultItems;

    // --- Runtime Data (Play 중 변화) ---

    // 실제 인벤토리 슬롯 배열 (Blueprint에서 조회 가능)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|State")
    TArray<FInventorySlot> Slots;

    // 현재 총 무게 (kg)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|State")
    float CurrentWeight = 0.0f;


    // --- Public API ---

    UNPCInventoryComponent();

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
    bool AddItem(UItemDataAsset* Item, int32 Amount = 1);

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
     * 현재 인벤토리 상태를 LLM이 이해할 수 있는 JSON 문자열로 반환합니다.
     * 예: [{"id": "Sword_01", "name": "Iron Sword", "count": 1}, ...]
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Context")
    FString GetInventoryJson() const;

    // 현재 무게 다시 계산 (디버그/검증용)
    UFUNCTION(BlueprintCallable, Category = "Inventory|Utils")
    void CalculateWeight();

private:
    // 내부 유틸: 빈 슬롯 찾기
    int32 FindEmptySlot() const;
    
    // 내부 유틸: 해당 아이템이 들어있는(스택 가능한) 슬롯 찾기
    int32 FindStackableSlot(UItemDataAsset* Item) const;
};
