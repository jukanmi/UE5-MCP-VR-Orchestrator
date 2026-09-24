#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "SmartNPCAIController.generated.h"

class UMCPStateTreeAIComponent;
class FJsonObject;

/** jevlike 전술 태도(Choice). 최상위 BehaviorMode 는 건드리지 않고 서브 상태 전이·가중치 편향에만 쓴다. */
UENUM(BlueprintType)
enum class EJevTacticalStance : uint8
{
    Default     UMETA(DisplayName = "Default"),     // 중립/기본(미도착·만료·저신뢰도)
    Aggressive  UMETA(DisplayName = "Aggressive"),  // 저돌적 압박
    Defensive   UMETA(DisplayName = "Defensive"),   // 방어적 거리유지
    Flee        UMETA(DisplayName = "Flee")         // 후퇴/도주
};

/** jevlike 회신 캐시. 기본값이 곧 중립 폴백(승수 1.0) — 미도착·타임아웃·만료 시 C++ 로직이 그대로 돈다. */
USTRUCT(BlueprintType)
struct UE5_MCP_VR_API FJevDecision
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    EJevTacticalStance Stance = EJevTacticalStance::Default;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float Confidence = 0.0f;

    /** Noul — 유해/탈옥 확률(0~1). FSTCondition_NoulGuard 가 임계값 검사. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float NoulHarmful = 0.0f;

    /** 전투 액션 가중치 배율(1.0 = 중립). Attack 후보·EQS AggressionWeight 에 곱함. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float ScoreAggression = 1.0f;

    /** Dodge/Flee 후보·EQS Cover/Distance 가중치에 곱함(1.0 = 중립). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jev")
    float ScoreCaution = 1.0f;

    /** 수신 시각(FPlatformTime::Seconds). -1 = 미수신. Evaluator 가 TTL 2.0s 만료 검사. */
    double ReceivedAt = -1.0;
};

/**
 * AI Controller for SmartNPC.
 * StateTree 단일 실행 + Blackboard(Perception 키 공유) + AIPerception(Sight/Hearing).
 */
UCLASS()
class UE5_MCP_VR_API ASmartNPCAIController : public AAIController
{
	GENERATED_BODY()

public:
	ASmartNPCAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

    /** StateTree AI Component. SmartNPC.StateTreeAsset을 OnPossess에서 주입. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UMCPStateTreeAIComponent* StateTreeAI;

    /** AI Perception Component for vision/hearing */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UAIPerceptionComponent* PerceptionComp;

    /** Sight Sense configuration */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UAISenseConfig_Sight* SightConfig;

    /** Hearing Sense configuration */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UAISenseConfig_Hearing* HearingConfig;

    /** Callback for perception updates */
    UFUNCTION()
    void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

    /** Callback events for NPCActionComponent */
    UFUNCTION()
    void HandleAllActionsStopped();

    /** 시야 유지 중 주기적 Cognition 재보고 */
    UFUNCTION()
    void OnPerceptionTick();

    /** 현재 시야 안에 있는 대상 (소실 시 null) */
    TWeakObjectPtr<AActor> CurrentSightTarget;

    /** 주기적 Perception 재보고 타이머 */
    FTimerHandle PerceptionTickTimer;

    /** Perception Tick 간격 (초) */
    UPROPERTY(EditDefaultsOnly, Category = "AI|Perception")
    float PerceptionTickInterval = 9.0f;

    /** Combat 중 시야 소실 시 전투 잔존 해제 타임아웃(초) — BB 타겟 null 이 이 시간 지속되면
     *  Common 복귀. 이내 재발견 시 타이머 취소(짧은 엄폐·스쳐 지나감은 전투 유지). */
    UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = "1.0", ClampMax = "60.0"))
    float CombatTargetLostTimeout = 8.0f;

    FTimerHandle CombatTargetLostTimer;

    /** 소실 타임아웃 만료 콜백 — 여전히 Combat + BB 타겟 null 이면 ExitCombat(nullptr). */
    void HandleCombatTargetLostTimeout();

    /** 주기 perception 재보고 중단 — 타이머 해제 + 시야 대상 리셋. 대상 소실·사망·틱 중 대상 무효 공통. */
    void StopSightTracking();

public:
	/** 전투 타겟 사망 판정 — ACombatCharacter::IsActorDead 위임(bIsDead / State.Condition.Dead 태그). */
	static bool IsTargetDead(const AActor* Target);

	/** 전투 종료 시퀀스: 진행 액션 중단·잔여 큐 폐기 → BehaviorMode=Common → replan 플래그 → BB.TargetActor 클리어.
	 *  DeadTarget 이 있으면(사망) 승리 보고 + 그 대상 시야 감시 해제, nullptr 이면(장기 소실) 보고만 생략.
	 *  BB 쓰기 소유권에 따라 STTask 가 아닌 컨트롤러가 수행 — STTask_PrepareNextAction 종료 게이트가 호출. */
	void ExitCombat(AActor* DeadTarget);

	/** 넉다운 중 AI 일시정지 — StateTree 정지 + 이동 중단. UnPossess 금지(재빙의·BB 손실 회피). */
	void PauseAI();

	/** 기상 후 AI 재개 — StateTree 재시작(루트부터 위협 재평가). BB·소유는 유지됨. */
	void ResumeAI();

	/** EQS 전술 가중치를 Blackboard 에 반영한다. Blackboard 쓰기는 이 클래스에서만 한다. */
	void UpdateEQSBlackboardParams(float SearchRadius, float CoverWeight, float DistanceWeight,
		float AggressionWeight, float SafeDistance);

	// ── Jevlike 전술 편향기 (캐시·세대·워치독은 컨트롤러가 독점 소유) ──────────────
	/** 최신 jevlike 결정(읽기 전용). 신선도(TTL) 판정은 호출측(Evaluator/셀렉터)이 ReceivedAt 으로 한다. */
	const FJevDecision& GetJevDecision() const { return JevDecision; }

	/** jevlike 승수 — TTL(JevDecisionTTL) 이내면 캐시, 만료·미수신이면 중립(1.0) 기본값. 셀렉터·EQS 공용 진입점. */
	FJevDecision GetFreshJevDecision() const;

	/** 감각 이벤트(시야 적대 감지·전투 소음·HP 25% 교차) 시 호출. 버리지 않고 예약만 한다 — 같은 프레임 호출은 1건으로 묶고,
	 *  쿨다운 중이면 만료 시점으로 미룬다. 전송은 SendCombatJevRequest 가 그 시점의 최신 지표로 하고,
	 *  진행 중인 요청(전투·daily)은 세대 증가로 폐기·교체된다 — 동시 감지 시 첫 감지(count=1)만 남던 문제 해소. */
	void RequestJevDecision();

	/** Python jev_decision payload 처리 — 세대 불일치·타임아웃 후 도착은 폐기, Confidence < 0.5 는 중립 유지. */
	void HandleJevDecisionResponse(const TSharedPtr<FJsonObject>& Payload);

	/** 0.3s 워치독 만료 — 세대를 올려 늦은 패킷을 폐기하고 in-flight 해제. */
	void HandleJevTimeout();

	/** Jev daily — 비전투·큐 빔·!bIsBusy 인 틱마다 STTask 가 호출. Idle 경과·LLM 직후·서버 연결·in-flight
	 *  게이트를 통과하면 daily 요청 1건. in-flight·세대·워치독은 전투 요청과 공유한다. */
	void TickJevDaily();

	/** Idle 이 아닐 때(액션 진행·전투) — Idle 경과를 처음부터 다시 잰다. */
	void ResetJevDailyIdle() { JevDailyIdleSince = -1.0; }

protected:
	FJevDecision JevDecision;
	uint32 JevGeneration = 0;
	bool bJevRequestInFlight = false;
	double LastJevRequestTime = 0.0;
	FTimerHandle JevTimeoutTimer;

	/** 예약된 전투 요청이 있는지 — 같은 프레임·쿨다운 중 중복 호출을 1건으로 묶는다. */
	bool bJevRequestPending = false;
	FTimerHandle JevPendingTimer;

	/** 예약된 전투 요청 실행 — 지표를 이 시점에 계산해 보낸다(진행 중 요청은 세대 증가로 교체). */
	void SendCombatJevRequest();

	/** 진행 중 요청이 daily 인지 — 응답 분기용. 전투 요청은 daily 를 끊고 들어간다. */
	bool bJevInFlightDaily = false;

	/** Idle 시작 시각(FPlatformTime). -1 = Idle 아님. */
	double JevDailyIdleSince = -1.0;

	/** 세대 증가·in-flight·워치독 가동 후 발송 — 전투·daily 공용. generation 필드는 여기서 채운다. */
	void SendJevQuery(const TSharedRef<FJsonObject>& Payload, bool bDaily);

	/** 비전투 Idle 이 이만큼 이어지면 daily 요청(초). */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Jev", meta = (ClampMin = "1.0"))
	float JevDailyIdleSeconds = 10.0f;

	/** 마지막 LLM 배치 후 이 시간(초) 동안은 daily 요청 안 함 — 대화 직후 끼어들기 방지. */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Jev", meta = (ClampMin = "0.0"))
	float JevDailyAfterLLMSeconds = 15.0f;

	/** 전송 최소 간격(초) — 전투 소음 폭주 방지. 이 사이 호출은 버리지 않고 만료 시점에 최신 지표로 1건 보낸다. */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Jev", meta = (ClampMin = "0.05"))
	float JevRequestCooldown = 0.25f;

	/** 응답 워치독(초) — 초과 시 늦은 패킷 폐기, 현재 C++ 액션 지속. */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Jev", meta = (ClampMin = "0.05"))
	float JevTimeoutSeconds = 0.3f;

	/** 결정 유효 시간(초) — 초과 시 중립으로 자동 롤백(오래된 도주 기조 오염 방지). */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Jev", meta = (ClampMin = "0.1"))
	float JevDecisionTTL = 2.0f;

	/** 이 미만 신뢰도는 편향 미적용(중립 유지). */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Jev", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JevMinConfidence = 0.5f;

public:

	// --- Blackboard Keys ---
	// Target Location Vector (e.g. for MoveTo)
	static const FName Key_TargetLocation;
	
    // Target Actor Object (e.g. for interacting/attacking)
	static const FName Key_TargetActor;

};
