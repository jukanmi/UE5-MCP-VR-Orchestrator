#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
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
    General     UMETA(DisplayName = "General"),
    Consumable  UMETA(DisplayName = "Consumable"),
    Equipment   UMETA(DisplayName = "Equipment"),
    Quest       UMETA(DisplayName = "Quest")
};

/**
 * 게임 내 모든 아이템의 변하지 않는 정적 데이터 정의.
 * - Blueprint에서 DataAsset을 생성하여 개별 아이템(DA_Sword, DA_Potion 등)을 만듭니다.
 * - 런타임 인벤토리 시스템은 이 에셋을 참조하여 아이템 정보를 가져옵니다.
 */
UCLASS(BlueprintType)
class UE5_MCP_VR_API UItemDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // --- Identity ---

    // 고유 식별자 (예: "HealthPotion_S", "Sword_Iron_01")
    // 시스템 내부 로직(Search, Remove 등)에서 Key로 사용됩니다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Identity")
    FString ItemID;

    // UI 표시 이름 (예: "Small Health Potion")
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Identity")
    FText DisplayName;

    // 아이템 설명 (LLM이 아이템의 용도를 이해하는 데 사용됨)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Identity", meta = (MultiLine = true))
    FString Description;

    // 아이템 카테고리
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Identity")
    EItemType ItemType = EItemType::General;

    // --- Stats ---

    // 무게 (kg 단위). 인벤토리 무게 제한 계산에 사용.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Stats")
    float Weight = 0.5f;

    // 한 슬롯에 최대 몇 개까지 겹쳐지는지 (장비는 보통 1)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Stats")
    int32 MaxStack = 64;

    // (선택) 기본 가치/가격
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Stats")
    int32 BaseValue = 10;

    // --- Visuals ---

    // UI 아이콘 (인벤토리 창 표시용)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Visual")
    TSoftObjectPtr<UTexture2D> Icon;

    // 월드에 버려지거나 스폰될 때 사용할 액터 클래스 (BP_ItemPickup 등)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Visual")
    TSoftClassPtr<AActor> WorldMeshClass;
};
