#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_Actions.generated.h"

/**
 * ============================================================================
 * ECommonAction - 공용/기본 액션
 * ============================================================================
 */
UENUM(BlueprintType)
enum class ECommonAction : uint8
{
	None           UMETA(DisplayName = "None"),
	Idle           UMETA(DisplayName = "Idle"),
	Move           UMETA(DisplayName = "Move"),
	Follow         UMETA(DisplayName = "Follow"),
	Wait           UMETA(DisplayName = "Wait"),
	Dialogue       UMETA(DisplayName = "Dialogue"),
	TurnTo         UMETA(DisplayName = "TurnTo"),
	Stop           UMETA(DisplayName = "Stop"),
	Scan           UMETA(DisplayName = "Scan"),
	UseItem        UMETA(DisplayName = "UseItem"),
	Equip          UMETA(DisplayName = "Equip"),
	Unequip        UMETA(DisplayName = "Unequip")
};

/**
 * ============================================================================
 * ECombatAction - 전투 모드 세부 액션
 * ============================================================================
 */
UENUM(BlueprintType)
enum class ECombatAction : uint8
{
	None             UMETA(DisplayName = "None"),
	Attack           UMETA(DisplayName = "Attack"),
	Block            UMETA(DisplayName = "Block"),
	Dodge            UMETA(DisplayName = "Dodge"),
	Flee             UMETA(DisplayName = "Flee"),
	SignalAllies     UMETA(DisplayName = "SignalAllies")
};

/**
 * ============================================================================
 * ESocialAction - 사교 모드 세부 액션
 * ============================================================================
 */
UENUM(BlueprintType)
enum class ESocialAction : uint8
{
	None         UMETA(DisplayName = "None"),
	Trade        UMETA(DisplayName = "Trade"),
	Follow       UMETA(DisplayName = "Follow"),
	Emote        UMETA(DisplayName = "Emote"),
	GiveItem     UMETA(DisplayName = "GiveItem"),
	Comfort      UMETA(DisplayName = "Comfort"),
	HandObject   UMETA(DisplayName = "HandObject")
};

/**
 * ============================================================================
 * ETaskAction - 작업 모드 세부 액션
 * ============================================================================
 */
UENUM(BlueprintType)
enum class ETaskAction : uint8
{
	None         UMETA(DisplayName = "None"),
	PickUp       UMETA(DisplayName = "PickUp"),
	Drop         UMETA(DisplayName = "Drop"),
	Craft        UMETA(DisplayName = "Craft"),
	Repair       UMETA(DisplayName = "Repair")
};

/**
 * ============================================================================
 * EInvestigationAction - 탐색 모드 세부 액션
 * ============================================================================
 */
UENUM(BlueprintType)
enum class EInvestigationAction : uint8
{
	None          UMETA(DisplayName = "None"),
	Investigate   UMETA(DisplayName = "Investigate"),
	Track         UMETA(DisplayName = "Track"),
	Scout         UMETA(DisplayName = "Scout")
};

/**
 * ============================================================================
 * ELifestyleAction - 생활 모드 세부 액션
 * ============================================================================
 */
UENUM(BlueprintType)
enum class ELifestyleAction : uint8
{
	None          UMETA(DisplayName = "None"),
	Sit           UMETA(DisplayName = "Sit"),
	Sleep         UMETA(DisplayName = "Sleep"),
	Clean         UMETA(DisplayName = "Clean"),
	Read          UMETA(DisplayName = "Read"),
	Pray          UMETA(DisplayName = "Pray"),
	Dance         UMETA(DisplayName = "Dance"),
	Sing          UMETA(DisplayName = "Sing")
};
