#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_CommonActions.generated.h"

/**
 * ============================================================================
 * ECommonAction - 공용/기본 액션
 * ============================================================================
 * 
 * 모든 모드에서 공통적으로 사용되거나, 특정 모드에 종속되지 않는 기본 행동들입니다.
 * Behavior Tree의 최상위나 병렬 노드, 또는 전용 Common 서브트리에서 처리됩니다.
 *
 * BT 서브트리 구조 예상:
 *   [Selector: Common Root]
 *     ├─ [Sequence: Idle]     → BTTask_CommonAction(Idle)
 *     ├─ [Sequence: Move]     → BTTask_CommonAction(Move)
 *     ├─ [Sequence: Wait]     → BTTask_CommonAction(Wait)
 *     ├─ [Sequence: Dialogue] → BTTask_CommonAction(Dialogue)
 *     ├─ [Sequence: TurnTo]   → BTTask_CommonAction(TurnTo)
 *     └─ [Sequence: UseItem]  → BTTask_CommonAction(UseItem)
 */
UENUM(BlueprintType)
enum class ECommonAction : uint8
{
	None           UMETA(DisplayName = "None"),

	/**
	 * Idle()
	 * 아무런 행동도 하지 않는 기본 대기 상태.
	 * Parameters: 없음
	 */
	Idle           UMETA(DisplayName = "Idle"),

	/**
	 * Move(TargetLocation, Speed)
	 * 특정 지점으로 이동한다. (단순 이동)
	 * Parameters:
	 *   - TargetLocation (FVector): 이동할 좌표 (Blackboard 값 사용)
	 *   - Speed          (float):   이동 속도 (생략 시 기본값)
	 */
	Move           UMETA(DisplayName = "Move"),
    
    /**
	 * Follow(TargetID, Distance)
	 * 대상을 따라간다. (Social Follow와 유사하지만 범용적)
	 * Parameters:
	 *   - TargetID  (FString): 따라갈 Actor ID
     *   - Distance  (float):   유지 거리
	 */
	Follow         UMETA(DisplayName = "Follow"),

	/**
	 * Wait(Duration)
	 * 제자리에서 일정 시간 동안 대기한다.
	 * Parameters:
	 *   - Duration  (float): 대기 시간 (초)
	 */
	Wait           UMETA(DisplayName = "Wait"),

	/**
	 * Dialogue(Content, Tone)
	 * 대사를 읊는다. (전투 중 외침, 혼잣말, 대화 등)
	 * Parameters:
	 *   - Content  (FString): 대사 내용
	 *   - Tone     (FString): 어조
	 */
	Dialogue       UMETA(DisplayName = "Dialogue"),

	/**
	 * TurnTo(TargetID)
	 * 특정 대상을 바라본다 (회전).
	 * Parameters:
	 *   - TargetID  (FString): 바라볼 대상 Actor ID
	 */
	TurnTo         UMETA(DisplayName = "TurnTo"),

	/**
	 * Stop()
	 * 현재 수행 중인 행동을 즉시 중단한다.
	 * Parameters: 없음
	 */
	Stop           UMETA(DisplayName = "Stop"),

	/**
	 * Scan()
	 * 주변을 두리번거리며 탐색한다. (시각적 연출)
	 * Parameters: 없음
	 */
	Scan           UMETA(DisplayName = "Scan"),

	/**
	 * UseItem(ItemID)
	 * 아이템을 사용한다. (포션 마시기, 음식 먹기 등)
	 * Parameters:
	 *   - ItemID  (FString): 사용할 아이템 ID
	 */
	UseItem        UMETA(DisplayName = "UseItem"),

	/**
	 * Equip(ItemID)
	 * 장비를 착용한다.
	 * Parameters:
	 *   - ItemID  (FString): 착용할 장비 ID
	 */
	Equip          UMETA(DisplayName = "Equip"),

	/**
	 * Unequip(ItemID)
	 * 장비를 해제한다.
	 * Parameters:
	 *   - ItemID  (FString): 해제할 장비 ID
	 */
	Unequip        UMETA(DisplayName = "Unequip")
};
