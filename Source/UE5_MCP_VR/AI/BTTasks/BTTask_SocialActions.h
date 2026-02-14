#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_SocialActions.generated.h"

/**
 * ============================================================================
 * ESocialAction - 사교 모드 세부 액션
 * ============================================================================
 * 
 * 발동 조건: 플레이어 접근, 대화 시도, 거래 요청, NPC 간 조우
 * 특징: LLM의 창의성이 가장 필요한 모드
 *
 * BT 서브트리 구조 예상:
 *   [Selector: Social Root]
 *     ├─ [Sequence: Dialogue] → BTTask_SocialAction(Dialogue)
 *     ├─ [Sequence: Trade]    → BTTask_SocialAction(Trade)
 *     ├─ [Sequence: Follow]   → BTTask_SocialAction(Follow)
 *     ├─ [Sequence: Emote]    → BTTask_SocialAction(Emote)
 *     ├─ [Sequence: Give]     → BTTask_SocialAction(GiveItem)
 *     ├─ [Sequence: Comfort]  → BTTask_SocialAction(Comfort)
 *     └─ [Sequence: Hand]     → BTTask_SocialAction(HandObject) ← VR 특화
 */
UENUM(BlueprintType)
enum class ESocialAction : uint8
{
	None         UMETA(DisplayName = "None"),



	/**
	 * Trade(TargetID, Offer, Request)
	 * 거래/협상을 시도한다.
	 * Parameters:
	 *   - TargetID  (FString): 거래 상대 Actor ID
	 *   - Offer     (FString): 제시하는 아이템/골드
	 *   - Request   (FString): 요구하는 아이템/골드
	 */
	Trade        UMETA(DisplayName = "Trade"),

	/**
	 * Follow(TargetID, Distance)
	 * 대상을 따라간다.
	 * Parameters:
	 *   - TargetID  (FString): 따라갈 Actor ID
	 *   - Distance  (float):   유지 거리 (cm)
	 */
	Follow       UMETA(DisplayName = "Follow"),

	/**
	 * Emote(GestureType)
	 * 감정 표현 제스처를 취한다 (인사, 끄덕임, 손짓 등).
	 * Parameters:
	 *   - GestureType  (FString): "Wave", "Nod", "Bow", "Shrug" 등
	 */
	Emote        UMETA(DisplayName = "Emote"),

	/**
	 * GiveItem(TargetID, ItemID)
	 * 대상에게 아이템을 건넨다 (선물/전달).
	 * Parameters:
	 *   - TargetID  (FString): 받는 Actor ID
	 *   - ItemID    (FString): 건넬 아이템 ID
	 */
	GiveItem     UMETA(DisplayName = "GiveItem"),

	/**
	 * Comfort(TargetID)
	 * 대상을 위로한다.
	 * Parameters:
	 *   - TargetID  (FString): 위로할 Actor ID
	 */
	Comfort      UMETA(DisplayName = "Comfort"),

	/**
	 * HandObject(TargetHand) — VR 특화
	 * VR 환경에서 플레이어의 손에 직접 물건을 건넨다.
	 * Parameters:
	 *   - TargetHand  (FString): "Left" 또는 "Right"
	 */
	HandObject   UMETA(DisplayName = "HandObject")
};
