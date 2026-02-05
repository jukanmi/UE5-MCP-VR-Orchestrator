#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
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

    // ToDo::AI Perception Component 추가 및 설정 (시각/청각 감지)
    // ToDo::TeamID 설정 (피아식별)

public:
	// --- Blackboard Keys ---
	// Target Location Vector (e.g. for MoveTo)
	static const FName Key_TargetLocation;
	
    // Action Type (Enum: ESmartNPCActionState)
	static const FName Key_ActionType;
	
    // Target Actor Object (e.g. for interacting/attacking)
	static const FName Key_TargetActor;
};
