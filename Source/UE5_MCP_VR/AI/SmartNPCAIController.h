#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "SmartNPCAIController.generated.h"

// Define Action State Enum
UENUM(BlueprintType)
enum class ESmartNPCActionState : uint8
{
    Idle        UMETA(DisplayName = "Idle"),
    Move        UMETA(DisplayName = "Move"),
    Speak       UMETA(DisplayName = "Speak"),
    Attack      UMETA(DisplayName = "Attack"),
    Interact    UMETA(DisplayName = "Interact"),
    Generic     UMETA(DisplayName = "Generic")
};

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

    // ToDo::AI Perception Component 추가 및 설정 (청각 감지)
    // ToDo::TeamID 설정 (피아식별)
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

public:
	// --- Blackboard Keys ---
	// Target Location Vector (e.g. for MoveTo)
	static const FName Key_TargetLocation;
	
    // Action Type (Enum: ESmartNPCActionState)
	static const FName Key_ActionType;
	
    // Target Actor Object (e.g. for interacting/attacking)
	static const FName Key_TargetActor;

	// Speech Text (for Speak action)
	static const FName Key_SpeakText;

	// --- New Orchestra Blackboard Keys ---
	/** High-level behavior mode (ENPCBehaviorMode) */
	static const FName Key_BehaviorMode;

	/** Specific action enum value (uint8) */
	static const FName Key_SubAction;

	/** JSON Parameters for the action */
	static const FName Key_ActionParameters;
};
