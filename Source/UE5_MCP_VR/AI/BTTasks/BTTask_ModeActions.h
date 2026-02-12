// BTTask_ModeActions.h
#pragma once

#include "CoreMinimal.h"
#include "BTTask_ModeActions.generated.h"

/**
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║ File: BTTask_ModeActions.h                                              ║
 * ║ Role: BEHAVIOR MODE & ACTION DEFINITIONS                               ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║ 이 파일은 NPC 행동 트리의 5가지 모드와 각 모드의 세부 액션을 정의합니다.   ║
 * ║                                                                          ║
 * ║ 아키텍처 (Section 8: LLM Orchestra):                                     ║
 * ║   1. Dialogue Agent → 큰 틀 (ENPCBehaviorMode) 결정                     ║
 * ║   2. Interface Output Agent → 세부 액션 (ActionName) 결정               ║
 * ║   3. UE5 Behavior Tree → 모드별 서브트리에서 액션 실행                   ║
 * ║                                                                          ║
 * ║ 이렇게 2단계로 나누는 이유:                                              ║
 * ║   - 하나의 LLM이 모든 행동을 결정하면 할루시네이션 확률이 높아짐         ║
 * ║   - 모드를 먼저 좁힌 후 세부 결정 → 선택 공간이 줄어 정확도 향상        ║
 * ║   - 각 모드별로 별도의 행동 트리 서브트리를 구성 가능                    ║
 * ║                                                                          ║
 * ║ 모드 구성:                                                               ║
 * ║   📂 1. Combat        - 전투 (Attack, Block, Dodge, Flee, ...)          ║
 * ║   📂 2. Social        - 사교 (Dialogue, Trade, Follow, Emote, ...)      ║
 * ║   📂 3. Task          - 작업 (PickUp, UseObject, Craft, ...)            ║
 * ║   📂 4. Investigation - 탐색 (Investigate, Track, Observe, ...)         ║
 * ║   📂 5. Lifestyle     - 생활 (Sit, Sleep, Read, IdleAction, ...)        ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */


// ============================================================================
// 📌 최상위 행동 모드 (Dialogue Agent가 결정)
// ============================================================================

/**
 * NPC의 5가지 행동 모드.
 * Dialogue Agent가 상황을 판단하여 이 중 하나를 선택한다.
 * Behavior Tree의 최상위 Selector에서 이 값으로 분기한다.
 *
 * 선택 기준:
 *   - Combat:        적 감지, 피격, 위협 수치 상승, 무기 꺼냄
 *   - Social:        플레이어 접근, 대화 시도, 거래 요청, NPC간 조우
 *   - Task:          주변 오브젝트 포커스, 아이템 루팅, 제작/수리 필요
 *   - Investigation: 수상한 소음, 시체 발견, 순찰 루트, 플레이어 놓침
 *   - Lifestyle:     Idle 상태, 밤 시간대, 휴식 필요, 스케줄 기반
 */
UENUM(BlueprintType)
enum class ENPCBehaviorMode : uint8
{
	None           UMETA(DisplayName = "None"),
	Combat         UMETA(DisplayName = "Combat"),         // 전투 모드
	Social         UMETA(DisplayName = "Social"),         // 사교 모드
	Task           UMETA(DisplayName = "Task"),           // 상호작용/작업 모드
	Investigation  UMETA(DisplayName = "Investigation"),  // 탐색/조사 모드
	Lifestyle      UMETA(DisplayName = "Lifestyle")       // 생활/대기 모드
};


// ============================================================================
// 📂 1. 전투 모드 (Combat Mode)
// ============================================================================

/**
 * 전투 모드 세부 액션.
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
	 * SignalAllies(Command)
	 * 아군에게 지원 요청 또는 명령을 보낸다.
	 * Parameters:
	 *   - Command  (FString): "Help", "Flank", "Retreat" 등
	 */
	SignalAllies     UMETA(DisplayName = "SignalAllies")
};


// ============================================================================
// 📂 2. 사교 모드 (Social Mode)
// ============================================================================

/**
 * 사교 모드 세부 액션.
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
	 * Dialogue(Content, Tone)
	 * 대화를 생성하고 발화한다.
	 * Parameters:
	 *   - Content  (FString): 대화 내용 (LLM이 생성한 텍스트)
	 *   - Tone     (FString): 어조 ("Friendly", "Hostile", "Nervous" 등)
	 */
	Dialogue     UMETA(DisplayName = "Dialogue"),

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


// ============================================================================
// 📂 3. 상호작용/작업 모드 (Task Mode)
// ============================================================================

/**
 * 작업 모드 세부 액션.
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


// ============================================================================
// 📂 4. 탐색/조사 모드 (Investigation Mode)
// ============================================================================

/**
 * 탐색 모드 세부 액션.
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


// ============================================================================
// 📂 5. 생활/대기 모드 (Lifestyle Mode)
// ============================================================================

/**
 * 생활 모드 세부 액션.
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

	/**
	 * IdleAction()
	 * 가벼운 대기 행동 (하품, 기지개, 두리번거림 등).
	 * Parameters: 없음 (랜덤 선택)
	 */
	IdleAction    UMETA(DisplayName = "IdleAction")
};


// ============================================================================
// 📦 모드 액션 요청 구조체 (Cognitive Engine → UE5)
// ============================================================================

/**
 * Dialogue Agent가 모드를, Interface Output Agent가 세부 액션을 결정한 후
 * UE5에 전달되는 요청 구조체.
 *
 * Cognitive Engine 파이프라인:
 *   Dialogue Agent     → Mode(Social)
 *   Interface Output   → ActionName("Dialogue"), TargetID("Player"), Parameters({Tone:"Friendly"})
 *   
 * UE5 수신 후 처리:
 *   1. Mode로 BT 서브트리 분기
 *   2. ActionName으로 해당 서브트리 내 Task 노드 선택
 *   3. Parameters를 Blackboard에 설정
 *   4. 행동 트리가 실행
 *
 * 기존 FGameAction과의 호환:
 *   FGameAction.ActionType  →  FModeActionRequest.ActionName (세부 액션)
 *   (없었음)                →  FModeActionRequest.Mode (큰 틀, 신규)
 *   FGameAction.TargetID    →  FModeActionRequest.TargetID (동일)
 *   FGameAction.Parameters  →  FModeActionRequest.Parameters (동일)
 */
USTRUCT(BlueprintType)
struct FModeActionRequest
{
	GENERATED_BODY()

	/** 행동 모드 큰 틀 (Dialogue Agent가 결정) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	ENPCBehaviorMode Mode = ENPCBehaviorMode::None;

	/** 세부 액션 이름 (Interface Output Agent가 결정) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	FString ActionName;

	/** 대상 Actor ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	FString TargetID;

	/** 유연한 파라미터 맵 (액션별로 다른 키 사용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	TMap<FString, FString> Parameters;
};
