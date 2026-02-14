#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_LifestyleActions.generated.h"

/**
 * ============================================================================
 * ELifestyleAction - 생활 모드 세부 액션
 * ============================================================================
 * 
 * 발동 조건: Idle 상태, 밤 시간대, 휴식 필요, 특정 스케줄
 * 특징: 게임의 생동감(Ambient)을 담당. NPC가 살아있는 느낌을 부여한다.
 *
 * BT 서브트리 구조 예상:
 *   [Selector: Lifestyle Root]
 *     ├─ [Sequence: Sit]    → BTTask_LifestyleAction(Sit)
 *     ├─ [Sequence: Sleep]  → BTTask_LifestyleAction(Sleep)
 *     ├─ [Sequence: Clean]  → BTTask_LifestyleAction(Clean)
 *     ├─ [Sequence: Read]   → BTTask_LifestyleAction(Read)
 *     ├─ [Sequence: Pray]   → BTTask_LifestyleAction(Pray)
 *     ├─ [Sequence: Dance]  → BTTask_LifestyleAction(Dance)
 *     ├─ [Sequence: Sing]   → BTTask_LifestyleAction(Sing)
 *     └─ [Sequence: Idle]   → BTTask_LifestyleAction(IdleAction)
 */
UENUM(BlueprintType)
enum class ELifestyleAction : uint8
{
	None          UMETA(DisplayName = "None"),

	/**
	 * Sit(ChairID)
	 * 의자/벤치에 앉는다.
	 * Parameters:
	 *   - ChairID  (FString): 앉을 오브젝트 ID
	 */
	Sit           UMETA(DisplayName = "Sit"),

	/**
	 * Sleep(BedID)
	 * 침대에서 수면을 취한다.
	 * Parameters:
	 *   - BedID  (FString): 잠들 침대 ID
	 */
	Sleep         UMETA(DisplayName = "Sleep"),

	/**
	 * Clean(Area)
	 * 구역을 청소한다.
	 * Parameters:
	 *   - Area  (FString): 청소할 구역 이름/ID
	 */
	Clean         UMETA(DisplayName = "Clean"),

	/**
	 * Read(BookID)
	 * 책을 읽는다.
	 * Parameters:
	 *   - BookID  (FString): 읽을 책 ID
	 */
	Read          UMETA(DisplayName = "Read"),

	/**
	 * Pray()
	 * 기도를 한다.
	 * Parameters: 없음
	 */
	Pray          UMETA(DisplayName = "Pray"),

	/**
	 * Dance()
	 * 춤을 춘다.
	 * Parameters: 없음 (또는 DanceStyle)
	 */
	Dance         UMETA(DisplayName = "Dance"),

	/**
	 * Sing()
	 * 노래를 부른다.
	 * Parameters: 없음 (또는 SongID)
	 */
	Sing          UMETA(DisplayName = "Sing"),


};
