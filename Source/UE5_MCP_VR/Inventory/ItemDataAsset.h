#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemDataAsset.generated.h"

/**
 * 아이템 유형 정의
 * - General: 일반 잡동사니 (돌, 나무 등)
 * - Consumable: 소비 아이템 (포션, 음식)
 * - Equipment: 장착 가능 아이템 (무기, 방어구)
 * - Quest: 퀘스트 중요 아이템 (열쇠, 문서)
 */
UENUM(BlueprintType)
enum class EItemType : uint8
{
    General     UMETA(DisplayName = "General"),    // 조합/납품용 잡동사니 (돌, 나무 등)
    Consumable  UMETA(DisplayName = "Consumable"), // 1회성 소모를 통해 효과를 주는 아이템 (포션, 음식)
    Equipment   UMETA(DisplayName = "Equipment"),  // 캐릭터 스탯 및 외형에 반영되는 장착품 (무기, 방어구)
    Quest       UMETA(DisplayName = "Quest")       // 버릴 수 없으며 시스템 제어를 받는 스토리 필수 아이템
};

/**
 * 장착 가능(Equipment) 아이템이 캐릭터의 어느 소켓/부위에 들어갈지를 명확히 규정하여,
 * 중복 착용을 방지하고 올바른 부위의 스탯 연산을 보장하기 위한 슬롯 정의입니다.
 */
UENUM(BlueprintType)
enum class EEquipmentSlot : uint8
{
    None        UMETA(DisplayName = "None"),
    MainHand    UMETA(DisplayName = "Main Hand"), // 우측 주 사용 무기/도구
    OffHand     UMETA(DisplayName = "Off Hand"),  // 좌측 보조 무기/방패
    Head        UMETA(DisplayName = "Head"),      // 투구 및 모자
    Torso       UMETA(DisplayName = "Torso"),     // 흉갑 및 셔츠
    Legs        UMETA(DisplayName = "Legs"),      // 바지 및 하의
    Feet        UMETA(DisplayName = "Feet"),      // 신발
    Gloves      UMETA(DisplayName = "Gloves"),    // 장갑
    Accessory   UMETA(DisplayName = "Accessory"), // 반지, 목걸이 등 마법 장신구
    Back        UMETA(DisplayName = "Back")       // 망토, 배낭 등 등짝 장착물
};

/**
 * 아이템 데이터 구조체 (데이터 테이블 행 기반)
 * - 게임 내 모든 아이템의 기본 속성을 정의하는 원본 템플릿입니다.
 * - DataTable에서 로드된 후, 인벤토리 슬롯 등에 복사되어 고유한 상태(내구도 등)를 갖습니다.
 */
USTRUCT(BlueprintType)
struct UE5_MCP_VR_API FItemData : public FTableRowBase
{
    GENERATED_BODY()

    // --- Identity ---

    // 아이템 고유 식별자 (DataTable의 RowName과 동일하게 맞추는 것을 권장)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
    FString ItemID;

    // UI 표시 이름
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
    FText DisplayName;

    // 아이템 설명 (LLM이 아이템의 용도를 이해하는 데 사용됨)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity", meta = (MultiLine = true))
    FString Description;

    // 아이템 카테고리
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
    EItemType ItemType = EItemType::General;

    // --- Stats ---

    // 무게 (kg 단위). 인벤토리 무게 제한 계산에 사용.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Stats")
    float Weight = 0.5f;

    // 한 슬롯에 최대 몇 개까지 겹쳐지는지 (장비는 보통 1)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Stats")
    int32 MaxStack = 64;

    // 장착 슬롯 (Equipment 타입일 경우에만 유효)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Stats", meta = (EditCondition = "ItemType == EItemType::Equipment"))
    EEquipmentSlot EquipSlot = EEquipmentSlot::None;

    // 기본 가치 (상점 판매, 분해 보상 등에 사용)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Stats")
    int32 BaseValue = 10;

    // --- Durability ---

    // 내구도 사용 여부
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Durability")
    bool bHasDurability = false;

    // 최대 내구도
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Durability", meta = (EditCondition = "bHasDurability"))
    float MaxDurability = 100.0f;

    // 현재 내구도 (인스턴스화 시 개별 값을 가짐)
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Item|Durability", meta = (EditCondition = "bHasDurability"))
    float CurrentDurability = 100.0f;

    // 내구도 회복 (구조체 멤버 함수는 복사된 인스턴스의 값을 변경함)
    void RepairItem(float Amount)
    {
        CurrentDurability += Amount;
        if (CurrentDurability > MaxDurability)
        {
            CurrentDurability = MaxDurability;
        }
    }

    // --- Visuals ---

    // UI 아이콘 (인벤토리 창 표시용)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Visual")
    TSoftObjectPtr<UTexture2D> Icon;

    // 월드에 표시될 3D 모델 (필드에 떨어뜨리거나 상호작용할 때 사용)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Visual")
    TSoftClassPtr<AActor> WorldMeshClass;
    
    // 유효성 검사용 (비어있는 구조체인지 확인)
    bool IsValidItem() const
    {
        return !ItemID.IsEmpty();
    }
};
