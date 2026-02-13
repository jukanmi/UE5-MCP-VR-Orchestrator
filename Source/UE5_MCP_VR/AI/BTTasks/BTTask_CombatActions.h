#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_CombatActions.generated.h"

/**
 * ============================================================================
 * ECombatAction - 전투 모드 세부 액션
 * ============================================================================
 * 
 * 발동 조건: 적 감지, 피격, 위협 수치 상승, 무기 꺼냄
 * 특징: 반응 속도가 빨라야 하므로 가벼운 모델 또는 룰 기반 개입이 필요할 수 있음
 *
 * BT 서브트리 구조 예상:
 *   [Selector: Combat Root]
 *     ├─ [Sequence: Attack]  → BTTask_CombatAction(Attack)
 *     ├─ [Sequence: Block]   → BTTask_CombatAction(Block)
 *     ├─ [Sequence: Dodge]   → BTTask_CombatAction(Dodge)
 *     ├─ [Sequence: Flee]    → BTTask_CombatAction(Flee)
 *     ├─ [Sequence: UseItem] → BTTask_CombatAction(UseCombatItem)
 *     └─ [Sequence: Signal]  → BTTask_CombatAction(SignalAllies)
 */
UENUM(BlueprintType)
enum class ECombatAction : uint8
{
	None             UMETA(DisplayName = "None"),

	/**
	 * Attack(TargetID, WeaponSlot)
	 * 대상을 공격한다.
	 * Parameters:
	 *   - TargetID    (FString): 공격 대상 Actor ID
	 *   - WeaponSlot  (int32):   사용할 무기 슬롯 (0=주무기, 1=보조)
	 */
	Attack           UMETA(DisplayName = "Attack"),

	/**
	 * Block(Duration)
	 * 방어/패링 자세를 취한다.
	 * Parameters:
	 *   - Duration  (float): 방어 유지 시간 (초)
	 */
	Block            UMETA(DisplayName = "Block"),

	/**
	 * Dodge(Direction)
	 * 회피 (구르기) 동작을 수행한다.
	 * Parameters:
	 *   - Direction  (FVector): 회피 방향 (Left/Right/Back 등)
	 */
	Dodge            UMETA(DisplayName = "Dodge"),

	/**
	 * Flee(DangerSourceLocation)
	 * 위험으로부터 도망한다 (전투 이탈).
	 * Parameters:
	 *   - DangerSourceLocation  (FVector): 도망 기준 위치 (위험원)
	 */
	Flee             UMETA(DisplayName = "Flee"),

	/**
	 * UseCombatItem(PotionID)
	 * 전투 중 급박한 아이템 사용 (포션 등).
	 * Parameters:
	 *   - PotionID  (FString): 사용할 아이템 ID
	 */
	UseCombatItem    UMETA(DisplayName = "UseCombatItem"),

	/**
	 * SignalAllies(SignalType)
	 * 아군에게 신호를 보낸다 (지원 요청, 포위 명령 등).
	 * Parameters:
	 *   - SignalType  (FString): "Help", "Surround", "Retreat" 등
	 */
	SignalAllies     UMETA(DisplayName = "SignalAllies")
};
