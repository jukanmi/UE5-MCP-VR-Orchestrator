#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "SmartNPCAIController.generated.h"

/**
 * AI Controller for SmartNPC.
 * Manages Blackboard interaction and runs the Behavior Tree.
 */
UCLASS()
class UE5_MCP_VR_API ASmartNPCAIController : public AAIController
{
	GENERATED_BODY()
	
public:
	ASmartNPCAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;

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

	// --- New Orchestra Blackboard Keys ---
	/** High-level behavior mode (ENPCBehaviorMode) */
	static const FName Key_BehaviorMode;

	/** Indicates if there is a pending action in the queue (bool) */
	static const FName Key_HasAction;

	/** Specific action enum value (EAction) */
	static const FName Key_SubAction;

	/** JSON Parameters for the action */
	static const FName Key_Parameters;

	/** Facial expression state (EFacialState) */
	static const FName Key_FacialState;
};
