#pragma once

#include "CoreMinimal.h"
#include "NPCActionTypes.generated.h"

/**
 * ============================================================================
 * [AI State Enums]
 * ============================================================================
 */

/** EFacialState: NPC의 감정 및 표정 상태 */
UENUM(BlueprintType)
enum class EFacialState : uint8
{
	Neutral         UMETA(DisplayName = "Neutral"),
	Happy           UMETA(DisplayName = "Happy"),
	Sad             UMETA(DisplayName = "Sad"),
	Angry           UMETA(DisplayName = "Angry"),
	Fear            UMETA(DisplayName = "Fear"),
	Surprised       UMETA(DisplayName = "Surprised"),
	Disgusted       UMETA(DisplayName = "Disgusted"),
	Tired           UMETA(DisplayName = "Tired"),
	Pain            UMETA(DisplayName = "Pain")
};

/** ENPCBehaviorMode: 최상위 행동 카테고리 (BT 서브트리 분기 기준) */
UENUM(BlueprintType)
enum class ENPCBehaviorMode : uint8
{
	Combat         UMETA(DisplayName = "Combat"),
	Social         UMETA(DisplayName = "Social"),
	Task           UMETA(DisplayName = "Task"),
	Investigation  UMETA(DisplayName = "Investigation"),
	Lifestyle      UMETA(DisplayName = "Lifestyle"),
	Common         UMETA(DisplayName = "Common")
};

/** EMoveType: NPC 이동 방식 정의 */
UENUM(BlueprintType)
enum class EMoveType : uint8
{
	Walk,
	Run,
	Sprint,
	Crouch
};

UENUM(BlueprintType)
enum class EAttackType : uint8
{
	Melee,
	Range,
	Magic
};

/** ESenseType: AI 퍼셉션 감각 종류 통합 관리 */
UENUM(BlueprintType)
enum class ESenseType : uint8
{
	None       UMETA(DisplayName = "None"),
	Sight      UMETA(DisplayName = "Sight"),
	Hearing    UMETA(DisplayName = "Hearing"),
	Other      UMETA(DisplayName = "Other")
};

UENUM(BlueprintType)
enum class EEntityType : uint8
{
	None,
	Player,
	NPC,
	Item,
	InteractableObject
};

/**
 * ============================================================================
 * [Detailed Action Enums]
 * ============================================================================
 */

// Legacy sub-action enums have been removed. Use EAction directly.
/** EAction: 모든 액션을 통합한 열거형 */
UENUM(BlueprintType)
enum class EAction : uint8
{
    //Common
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
	Unequip        UMETA(DisplayName = "Unequip"),

    //Combat
	Attack           UMETA(DisplayName = "Attack"),
	Block            UMETA(DisplayName = "Block"),
	Dodge            UMETA(DisplayName = "Dodge"),
	Flee             UMETA(DisplayName = "Flee"),
	SignalAllies     UMETA(DisplayName = "SignalAllies"),

    //Social
	Trade        UMETA(DisplayName = "Trade"),
	Emote        UMETA(DisplayName = "Emote"),
	GiveItem     UMETA(DisplayName = "GiveItem"),
	Comfort      UMETA(DisplayName = "Comfort"),
	HandObject   UMETA(DisplayName = "HandObject"),

    //Task
	PickUp       UMETA(DisplayName = "PickUp"),
	Drop         UMETA(DisplayName = "Drop"),
	Craft        UMETA(DisplayName = "Craft"),
	Repair       UMETA(DisplayName = "Repair"),

    //Investigation
	Investigate   UMETA(DisplayName = "Investigate"),
	Track         UMETA(DisplayName = "Track"),
	Scout         UMETA(DisplayName = "Scout"),

    //Lifestyle
	Sit           UMETA(DisplayName = "Sit"),
	Sleep         UMETA(DisplayName = "Sleep"),
	Clean         UMETA(DisplayName = "Clean"),
	Read          UMETA(DisplayName = "Read"),
	Pray          UMETA(DisplayName = "Pray"),
	Dance         UMETA(DisplayName = "Dance"),
	Sing          UMETA(DisplayName = "Sing"),
};
/**
 * ============================================================================
 * [Data Structures]
 * ============================================================================
 */

/** FGameAction: NPCManager에서 수신된 Raw 액션 */
USTRUCT(BlueprintType)
struct FGameAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	EAction ActionType = EAction::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	EFacialState FacialState = EFacialState::Neutral;

	//예: "TargetID", "Location"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	TMap<FString, FString> Parameters;
};

/** FActionBatch: NPCManager에서 특정 NPC에게 전달된 액션 묶음 */
USTRUCT(BlueprintType)
struct FActionBatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	FString AgentID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	ENPCBehaviorMode Mode = ENPCBehaviorMode::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	TArray<FGameAction> Actions;
};

/** FModeActionRequest: interfaceOutput에서 NPCManager로 전달되는 NPC뭉터기들이 뭘할지 정해주는 정형화된 요청 데이터 */
USTRUCT(BlueprintType)
struct FModeActionRequest
{
	GENERATED_BODY()

	/** 대분류 행동 모드 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	ENPCBehaviorMode Mode = ENPCBehaviorMode::Common;

	/** NPCAgentID, ActionBatch */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Action")
	TMap<FString, FActionBatch> ActionBatches;
};
