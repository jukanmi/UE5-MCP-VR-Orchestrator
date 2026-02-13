#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.h"
#include "BTTask_InvestigationActions.generated.h"

/**
 * ============================================================================
 * EInvestigationAction - 탐색 모드 세부 액션
 * ============================================================================
 * 
 * 발동 조건: 수상한 소음, 시체 발견, 순찰 루트 진입, 플레이어 놓침
 * 특징: Perception 정보를 분석하고 추론하는 모드
 *
 * BT 서브트리 구조 예상:
 *   [Selector: Investigation Root]
 *     ├─ [Sequence: Investigate] → BTTask_InvestigationAction(Investigate)
 *     ├─ [Sequence: Track]       → BTTask_InvestigationAction(Track)
 *     ├─ [Sequence: Observe]     → BTTask_InvestigationAction(Observe)
 *     ├─ [Sequence: Scout]       → BTTask_InvestigationAction(Scout)
 *     └─ [Sequence: Report]      → BTTask_InvestigationAction(Report)
 */
UENUM(BlueprintType)
enum class EInvestigationAction : uint8
{
	None          UMETA(DisplayName = "None"),

	/**
	 * Investigate(Location)
	 * 의심 지점으로 이동하여 수색한다.
	 * Parameters:
	 *   - Location  (FVector): 수색할 위치
	 */
	Investigate   UMETA(DisplayName = "Investigate"),

	/**
	 * Track(TargetTrace)
	 * 흔적을 추적한다.
	 * Parameters:
	 *   - TargetTrace  (FString): 추적할 흔적 유형 ("Footprint", "Blood" 등)
	 */
	Track         UMETA(DisplayName = "Track"),

	/**
	 * Observe(TargetID)
	 * 대상을 관찰하여 정보를 수집한다 (적 감시 등).
	 * Parameters:
	 *   - TargetID  (FString): 관찰 대상 Actor ID
	 */
	Observe       UMETA(DisplayName = "Observe"),

	/**
	 * Scout(AreaRadius)
	 * 주변 반경을 정찰한다.
	 * Parameters:
	 *   - AreaRadius  (float): 정찰 반경 (cm)
	 */
	Scout         UMETA(DisplayName = "Scout"),

	/**
	 * Report(Info)
	 * 발견한 정보를 아군 NPC에게 보고/공유한다.
	 * Parameters:
	 *   - Info  (FString): 보고할 정보 내용
	 */
	Report        UMETA(DisplayName = "Report")
};
