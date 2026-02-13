#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_TaskActions.generated.h"

/**
 * ============================================================================
 * ETaskAction - 작업 모드 세부 액션
 * ============================================================================
 * 
 * 발동 조건: 주변 오브젝트 포커스, 아이템 루팅, 제작/수리 필요 시
 * 특징: 물리적인 세계와의 상호작용이 주를 이룸
 *
 * BT 서브트리 구조 예상:
 *   [Selector: Task Root]
 *     ├─ [Sequence: PickUp]    → BTTask_TaskAction(PickUp)
 *     ├─ [Sequence: Drop]      → BTTask_TaskAction(Drop)
 *     ├─ [Sequence: UseObject] → BTTask_TaskAction(UseObject)
 *     ├─ [Sequence: Craft]     → BTTask_TaskAction(Craft)
 *     ├─ [Sequence: Repair]    → BTTask_TaskAction(Repair)
 *     └─ [Sequence: EatDrink]  → BTTask_TaskAction(EatDrink)
 */
UENUM(BlueprintType)
enum class ETaskAction : uint8
{
	None         UMETA(DisplayName = "None"),

	/**
	 * PickUp(ItemID)
	 * 바닥의 아이템을 줍는다.
	 * Parameters:
	 *   - ItemID  (FString): 주울 아이템 ID
	 */
	PickUp       UMETA(DisplayName = "PickUp"),

	/**
	 * Drop(ItemID)
	 * 인벤토리의 아이템을 버린다.
	 * Parameters:
	 *   - ItemID  (FString): 버릴 아이템 ID
	 */
	Drop         UMETA(DisplayName = "Drop"),

	/**
	 * UseObject(ObjectID)
	 * 오브젝트와 상호작용한다 (문 열기, 레버 당기기 등).
	 * Parameters:
	 *   - ObjectID  (FString): 상호작용할 오브젝트 ID
	 */
	UseObject    UMETA(DisplayName = "UseObject"),

	/**
	 * Craft(RecipeID)
	 * 레시피에 따라 아이템을 제작한다.
	 * Parameters:
	 *   - RecipeID  (FString): 제작 레시피 ID
	 */
	Craft        UMETA(DisplayName = "Craft"),

	/**
	 * Repair(TargetObject)
	 * 손상된 오브젝트/장비를 수리한다.
	 * Parameters:
	 *   - TargetObject  (FString): 수리 대상 오브젝트 ID
	 */
	Repair       UMETA(DisplayName = "Repair"),

	/**
	 * EatDrink(ItemID)
	 * 비전투 상황에서 음식/음료를 섭취한다.
	 * Parameters:
	 *   - ItemID  (FString): 소비할 아이템 ID
	 */
	EatDrink     UMETA(DisplayName = "EatDrink")
};
