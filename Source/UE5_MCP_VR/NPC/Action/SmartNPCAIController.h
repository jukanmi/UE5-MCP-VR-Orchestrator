#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "SmartNPCAIController.generated.h"

class UMCPStateTreeAIComponent;

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
    void HandleActionStarted(const FGameAction& Action);

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

    /** 소실 타임아웃 만료 콜백 — 여전히 Combat + BB 타겟 null 이면 전투 해제.
     *  HandleCombatTargetDead 와 동일 시퀀스에서 승리 보고만 제외. */
    void HandleCombatTargetLostTimeout();

public:
	/** 전투 타겟 사망 판정 — SmartNPC.bIsDead / State.Condition.Dead 태그(플레이어 계열). */
	static bool IsTargetDead(const AActor* Target);

	/** 전투 종료 시퀀스: 진행 액션 중단·잔여 큐 폐기 → BehaviorMode=Common → replan 플래그 → BB.TargetActor 클리어.
	 *  BB 쓰기 소유권에 따라 STTask 가 아닌 컨트롤러가 수행 — STTask_PrepareNextAction 종료 게이트가 호출. */
	void HandleCombatTargetDead(AActor* DeadTarget);

	/** 넉다운 중 AI 일시정지 — StateTree 정지 + 이동 중단. UnPossess 금지(재빙의·BB 손실 회피). */
	void PauseAI();

	/** 기상 후 AI 재개 — StateTree 재시작(루트부터 위협 재평가). BB·소유는 유지됨. */
	void ResumeAI();

	// --- Blackboard Keys ---
	// Target Location Vector (e.g. for MoveTo)
	static const FName Key_TargetLocation;
	
    // Target Actor Object (e.g. for interacting/attacking)
	static const FName Key_TargetActor;

};
