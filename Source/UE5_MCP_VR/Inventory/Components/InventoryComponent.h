#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/BP/ItemDataAsset.h"
#include "InventoryComponent.generated.h"

/**
 * 인벤토리의 한 슬롯을 정의하는 구조체 (슬롯 번호 없음, 배열 인덱스 기반).
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

/** 인벤토리 내용 변경 알림 — 슬롯/장비/내구도 변동 시 브로드캐스트. HUD 등 UI가 바인딩. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);

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

    // 한 슬롯에 쌓을 수 있는 상한. 아이템 데이터의 MaxStack 과 비교해 작은 쪽이 실제 상한이 된다.
    // 마스터 테이블은 돌멩이 64·빵 16처럼 종류마다 제각각인데, 슬롯 UI 는 두 자리 수를
    // 넘어가면 읽기 어렵고 소지 한도도 종류에 따라 들쭉날쭉해진다. 여기서 일괄로 눌러 둔다.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config", meta = (ClampMin = "1"))
    int32 MaxStackLimit = 10;

    // 튜토리얼이나 기본 지급 장비를 일괄 적용하기 위해 스폰 시점에 미리 가져올 에셋들의 DataTable Row 이름입니다.
    // GetOptions — 에디터에서 직접 타이핑하는 대신 DT_ItemRegistry 행 목록에서 고른다.
    // 오타는 조회 실패로 조용히 지급 누락이 되므로(로그만 남음) 입력 자체를 막는 편이 낫다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config",
              meta = (GetOptions = "/Script/UE5_MCP_VR.ItemRegistryOptions.GetItemIDOptions"))
    TArray<FString> InitialDefaultItems;

    // 드랍 시 스폰할 액터 클래스. 아이템이 FItemData::WorldMeshClass 로 자기 BP 를 지정했다면 그쪽이 우선한다.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    TSubclassOf<class ADroppedItemBase> DroppedItemClass;

    // WorldMesh·WorldMeshClass 가 모두 빈 아이템의 드랍·장착 폴백 메시(기본 큐브).
    // 아이템 72종의 메시 에셋이 아직 없어서, 이게 없으면 드랍 액터가 보이지 않는다.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    TSoftObjectPtr<UStaticMesh> DefaultDropMesh;

    // 장착 메시를 붙일 소켓 이름. 스켈레톤이 바뀌어도 C++ 수정 없이 대응하도록 노출.
    // 기본값은 Mixamo X_Bot 손 본 이름 — 이 스켈레톤엔 손 소켓 에셋이 없고(2026-08-31 실측),
    // 부착은 소켓이 없으면 동명 본을 찾으므로 본 이름으로 붙인다(MeleeSphere 부착과 같은 방식).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    FName MainHandSocket = TEXT("RightHand");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    FName OffHandSocket = TEXT("LeftHand");

    // 손에 쥔 아이템의 손 본 기준 위치·회전 보정. 아이템 데이터에 값이 있으면 그쪽이 이기고,
    // 아무 값도 없는 아이템에만 적용되는 기본치다. 메시 원점이 제각각이라 실기에서 맞춰야 한다.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    FVector DefaultHoldOffset = FVector::ZeroVector;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Config")
    FRotator DefaultHoldRotation = FRotator::ZeroRotator;

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

    /** 손별로 지금 쥐고 있는 월드 아이템. 키는 MainHand(오른손)/OffHand(왼손).
     *  장착 슬롯과 같은 계열의 보유 상태라 여기서 관리한다 — 폰이 들고 있으면 NPC·상자 등
     *  다른 소유자는 같은 동작을 다시 구현해야 한다. 부착 메시(AttachedMeshes)와 같은 키 방식. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Inventory|State")
    TMap<EEquipmentSlot, TObjectPtr<ADroppedItemBase>> HeldItems;

    /** 슬롯 → 손 소켓 이름. MainHand/OffHand 외 부위는 방어구 미도입이라 NAME_None. */
    UFUNCTION(BlueprintPure, Category = "Inventory|Hand")
    FName GetSocketNameForSlot(EEquipmentSlot Slot) const;

    /** 해당 손에 쥔 아이템(없으면 nullptr). */
    UFUNCTION(BlueprintPure, Category = "Inventory|Hand")
    ADroppedItemBase* GetHeldItem(EEquipmentSlot HandSlot) const;

    /** 인벤토리 변경 이벤트 — Add/Remove/Equip/Unequip 성공 시 브로드캐스트. */
    UPROPERTY(BlueprintAssignable, Category = "Inventory|Event")
    FOnInventoryChanged OnInventoryChanged;

    /** UI 에서 지금 고른 슬롯 인덱스. 폰(스틱 조작)이 쓰고 HUD 위젯이 읽어 강조 표시한다.
     *  폰이 아니라 여기 있는 이유: HUD 위젯이 구체 폰 타입을 모르게 설계돼 있어(컴포넌트로만 접근)
     *  선택 상태를 폰에 두면 위젯이 읽을 길이 없다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Inventory|State")
    int32 SelectedSlotIndex = 0;

    /** 선택 슬롯 지정 — 범위를 벗어나면 순환한다. 변경 시 OnInventoryChanged 로 UI 갱신. */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
    void SetSelectedSlot(int32 NewIndex);


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
     * 소비 아이템을 1개 사용합니다.
     * - 회복량(HealthRestore/ManaRestore/StaminaRestore)을 소유자에게 적용하고 수량을 차감합니다.
     * - Consumable 이 아니거나 소유자가 ICharacterBase 가 아니면 차감 없이 실패(false).
     * - 회복량이 전부 0 이어도 성공합니다(연막탄처럼 회복이 아닌 소비템).
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
    bool UseItem(const FString& ItemID);

    /**
     * 아이템을 월드에 버립니다.
     * - 소유자 전방 발밑에 드랍 액터를 스폰한 뒤 인벤토리에서 차감합니다.
     * - 스폰이 실패하면 차감하지 않습니다(아이템 증발 방지).
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
    bool DropItem(const FString& ItemID, int32 Amount = 1);

    /**
     * 고른 슬롯을 발동합니다. (인벤토리 UI 진입점)
     * - Consumable 이면 UseItem, 그 외에는 무기든 잡템이든 HandSlot 손에 물리 액터로 쥡니다.
     * - Quest 는 손에 꺼내면 던져서 버릴 수 있게 되므로 거부(false).
     * - UI 가 아이템 종류를 보고 분기하지 않게 하려는 것 — 분기 규칙이 늘어나도 C++ 한 곳만 고친다.
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Action")
    bool ActivateItem(const FString& ItemID, EEquipmentSlot HandSlot = EEquipmentSlot::MainHand);

    /**
     * 아이템 1종을 월드 액터로 스폰한다(인벤토리는 건드리지 않는다).
     * 드랍(발밑)과 손에 꺼내기(손 위치)가 스폰 클래스 결정·메시 주입·ItemManager 등록 절차를
     * 공유하도록 분리해 둔 것. 차감은 호출측 책임 — 스폰 성공을 확인한 뒤에 빼야 증발하지 않는다.
     */
    ADroppedItemBase* SpawnItemActor(const FItemData& Data, const FString& ItemID,
                                     const FTransform& SpawnTransform, int32 Amount);

    // --- 손에 쥐기 (Hand) ---

    /** 월드 아이템을 해당 손 소켓에 쥔 상태로 만든다 — 물리·콜리전을 끄고 스냅 부착.
     *  주워서 쥘 때와 인벤토리에서 꺼내 쥘 때가 같은 상태로 수렴해야 놓기가 한 경로로 끝난다. */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Hand")
    void AttachItemToHand(ADroppedItemBase* Item, EEquipmentSlot HandSlot = EEquipmentSlot::MainHand);

    /** 해당 손에 쥔 것을 떼어 돌려준다(물리는 복구하지 않는다 — 던질지 넘길지는 호출측이 정한다).
     *  쥔 게 없으면 nullptr. */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Hand")
    ADroppedItemBase* ReleaseHeldItem(EEquipmentSlot HandSlot = EEquipmentSlot::MainHand);

    /** 인벤토리 슬롯의 아이템 1개를 월드 액터로 꺼내 해당 손에 쥔다. 스폰 성공 후에만 차감한다.
     *  그 손이 이미 차 있거나 퀘스트 아이템이면 실패. */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Hand")
    bool TakeItemToHand(const FString& ItemID, const FTransform& HandTransform,
                        EEquipmentSlot HandSlot = EEquipmentSlot::MainHand);

    /** 해당 손에 쥔 아이템을 인벤토리에 넣고 월드 액터를 파괴한다.
     *  가득 찼거나 데이터가 없으면 물리를 되살려 그 자리에 떨군다(증발 방지). */
    UFUNCTION(BlueprintCallable, Category = "Inventory|Hand")
    bool StoreHeldItem(EEquipmentSlot HandSlot = EEquipmentSlot::MainHand);

    /**
     * 특정 아이템을 일정 수량 이상 가지고 있는지 확인합니다.
     */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    bool HasItem(const FString& ItemID, int32 Amount = 1);

    /**
     * 특정 아이템의 총 보유 수량 (인벤토리 + 장착 중). HasItem 과 같은 기준입니다.
     */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    int32 GetItemCount(const FString& ItemID) const;

    /**
     * 특정 아이템 중 인벤토리 슬롯에만 들어 있는 수량 (장착분 제외).
     * 버리기·건네주기처럼 실제로 슬롯에서 빼내야 하는 동작의 판정 기준입니다.
     */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    int32 GetItemCountInSlots(const FString& ItemID) const;

    /** 아이템이 들어 있는 슬롯 수. */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    int32 GetUsedSlotCount() const;

    /** 비어 있는 슬롯 수. */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    int32 GetFreeSlotCount() const;

    /** 인벤토리에 든 아이템 총 개수(수량 합계, 장착분 제외). */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    int32 GetTotalItemCount() const;

    /**
     * 이 아이템을 지금 몇 개까지 더 받을 수 있는지 (슬롯 여유 + 스택 상한 기준, 무게 미포함).
     */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    int32 GetRemainingCapacityFor(const FItemData& Item) const;

    /** 이 아이템의 실제 스택 상한 = min(아이템 데이터 MaxStack, MaxStackLimit). */
    UFUNCTION(BlueprintPure, Category = "Inventory|Check")
    int32 GetEffectiveMaxStack(const FItemData& Item) const;

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

    /** 특정 아이템이 현재 장비 슬롯에 장착되어 있는지 여부. */
    UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
    bool IsItemEquipped(const FString& ItemID) const;

    /** 특정 아이템이 장착된 슬롯 반환 (미장착 시 EEquipmentSlot::None). */
    UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
    EEquipmentSlot GetSlotOfEquippedItem(const FString& ItemID) const;


    // 현재 무게 다시 계산 (디버그/검증용)
    UFUNCTION(BlueprintCallable, Category = "Inventory|Utils")
    void CalculateWeight();

protected:
    // 장착 슬롯별로 소유자 메시에 붙여둔 비주얼 컴포넌트. 해제 시 파괴 대상을 찾는 유일한 근거.
    UPROPERTY(Transient)
    TMap<EEquipmentSlot, TObjectPtr<UStaticMeshComponent>> AttachedMeshes;

    // 드랍·장착에 쓸 메시 결정: WorldMesh → (개별 BP 경로가 아니면) DefaultDropMesh 순.
    UStaticMesh* ResolveItemMesh(const FItemData& Data) const;

    // 장착 아이템 메시를 소유자 손 소켓에 부착. 소켓·메시가 없으면 경고만 남기고 건너뛴다
    // (비주얼 실패가 장착 자체를 막으면 안 된다).
    void AttachEquipmentMesh(EEquipmentSlot Slot, const FItemData& Data);

    // 해당 슬롯에 부착해 둔 메시 컴포넌트 파괴. 슬롯 교체는 EquipItem 이 먼저 부르는
    // UnequipItem 을 타고 여기로 오므로, 부착 경로에서 중복 파괴하지 말 것.
    void DetachEquipmentMesh(EEquipmentSlot Slot);

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
