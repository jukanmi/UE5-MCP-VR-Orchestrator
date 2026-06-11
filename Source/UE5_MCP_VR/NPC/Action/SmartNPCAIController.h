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

public:
	// --- Blackboard Keys ---
	// Target Location Vector (e.g. for MoveTo)
	static const FName Key_TargetLocation;
	
    // Target Actor Object (e.g. for interacting/attacking)
	static const FName Key_TargetActor;

};
