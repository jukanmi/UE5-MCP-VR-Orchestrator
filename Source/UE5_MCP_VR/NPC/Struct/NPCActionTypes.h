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

/** ENPCBehaviorMode: 최상위 행동 카테고리 (STTask 자율 행동 분기 기준) */
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

/** ENPCRelation: 호감도 임계값으로 분류한 대상과의 관계.
 *  단일 소유는 NPCStateComponent::GetRelation — GetAffinityMultiplier 도 이 값을 배율로 변환할 뿐이다.
 *  Any 는 반사 룰 필터 전용 와일드카드로, GetRelation 이 반환하는 일은 없다. */
UENUM(BlueprintType)
enum class ENPCRelation : uint8
{
	Hostile    UMETA(DisplayName = "Hostile"),
	Neutral    UMETA(DisplayName = "Neutral"),
	Friendly   UMETA(DisplayName = "Friendly"),
	Any        UMETA(DisplayName = "Any (rule filter only)")
};

/** ESenseType: AI 퍼셉션 감각 종류 통합 관리 */
UENUM(BlueprintType)
enum class ESenseType : uint8
{
	None       UMETA(DisplayName = "None"),
	Sight      UMETA(DisplayName = "Sight"),
	Hearing    UMETA(DisplayName = "Hearing"),
	Hit        UMETA(DisplayName = "Hit"),
	Other      UMETA(DisplayName = "Other")
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

    //Investigation
	Investigate   UMETA(DisplayName = "Investigate"),
	Track         UMETA(DisplayName = "Track"),
	Scout         UMETA(DisplayName = "Scout"),

    //Lifestyle
	Sit           UMETA(DisplayName = "Sit"),
	Sleep         UMETA(DisplayName = "Sleep"),
	// 자세 해제 — Sit/Sleep 의 짝. 진입만 있고 해제가 없으면 LLM 이 "일어나"를 표현할
	// 액션이 없어 Sleep 을 반복한다(2026-08-02 Moca 로그 실측).
	StandUp       UMETA(DisplayName = "StandUp"),
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

/** FReflexRule: 척수반사 테이블 한 줄.
 *
 *  자극(감각·소음종류·관계·거리·위험도)이 전부 맞으면 가중 분포로 액션 하나를 뽑아
 *  ActionQueue 에 직접 주입한다. Python 왕복이 없으므로 서버가 죽어 있어도 동작한다.
 *
 *  danger 필터가 **BaseDanger(자극 원본 세기)** 기준인 것이 중요하다. 최종 위험도
 *  (= BaseDanger × 호감도배율)는 Friendly 대상에서 항상 0 이 되므로, 최종값으로 걸면
 *  Friendly 룰은 영원히 안 걸린다. 관계는 Relation 이, 세기는 BaseDanger 가 따로 본다.
 */
USTRUCT(BlueprintType)
struct FReflexRule
{
	GENERATED_BODY()

	/** 어떤 감각의 자극에 반응하는가. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex")
	ESenseType Sense = ESenseType::Sight;

	/** Hearing 소음 태그 부분매치("Drop", "UseItem" 등). 비어 있으면 종류를 안 가린다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex")
	FString EventTypeContains;

	/** 대상과의 관계. Any = 무관. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex")
	ENPCRelation Relation = ENPCRelation::Any;

	/** 거리 상한(cm). 0 이면 무제한. 감지 반경(SightRadius/HearingRange = 3000)보다 크게 잡지 말 것. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex", meta = (ClampMin = "0.0"))
	float MaxDistance = 0.f;

	/** BaseDanger 하한(포함). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinBaseDanger = 0.f;

	/** 이 룰의 재발동 금지 시간(초). perception tick 이 9초마다 같은 자극을 재통지하므로
	 *  생동 반응류는 그보다 넉넉히 잡아야 스팸이 안 난다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex", meta = (ClampMin = "0.0"))
	float Cooldown = 3.f;

	/** 액션별 가중치. 합이 1일 필요 없다(상대 비율로 추첨). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex")
	TMap<EAction, float> ActionWeights;

	/** true 면 발동과 동시에 BehaviorMode 를 Combat 으로 올린다. 비전투 반사는 반드시 false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex")
	bool bEnterCombat = false;

	/** 로그·디버그용 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex")
	FString RuleName;
};

// ============================================================================
// [전술 위치 결정 시스템 - EQS + LLM 분업 구조]
// WHY: 모든 이동 결정을 LLM에 위임하면 지연이 크다.
//      C++에서 EQS로 후보를 생성·스코어링하고, 최종 판단만 LLM에 위임하여
//      LLM 호출 횟수와 페이로드 크기를 모두 줄인다.
// ============================================================================

/** ELocationCategory: 전술 위치 후보 분류 */
UENUM(BlueprintType)
enum class ELocationCategory : uint8
{
    Safe        UMETA(DisplayName = "Safe"),       // 방어·은폐 우선
    Optimal     UMETA(DisplayName = "Optimal"),    // 교전 최적 포지션
    Aggressive  UMETA(DisplayName = "Aggressive"), // 근접·돌격 우선
};

/** FLocationCandidate: EQS 결과 위치 + 로컬 스코어링 결과 */
USTRUCT(BlueprintType)
struct FLocationCandidate
{
    GENERATED_BODY()

    /** "SAFE_0", "OPTIMAL_1" 형태 - LLM 응답에서 식별자로 사용 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Tactical")
    FString CandidateId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Tactical")
    ELocationCategory Category = ELocationCategory::Optimal;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Tactical")
    FVector Location = FVector::ZeroVector;

    /** 카테고리 내 스코어 (높을수록 우선) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Tactical")
    float Score = 0.f;

    /** 가장 가까운 적과의 거리 (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Tactical")
    float DistanceToEnemy = 0.f;

    /** 0 = 완전 노출, 1 = 완전 은폐 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Tactical")
    float CoverRating = 0.f;

    /** 적 대비 높이 차이 (양수 = NPC가 높음) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Tactical")
    float HeightDelta = 0.f;
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
