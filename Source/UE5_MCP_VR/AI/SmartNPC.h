#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BehaviorTree/BehaviorTree.h"
#include "../Utils/MCPJsonUtils.h" // For FGameAction struct
#include "BTTasks/BTTask_BaseDefinitions.h" // For FModeActionRequest and Enums
#include "CharacterAttributes.h"
#include "SmartNPC.generated.h"

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API ASmartNPC : public ACharacter
{
    GENERATED_BODY()

public:
    ASmartNPC();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // Unique ID for routing (e.g. "Guard_1")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI")
    FString AgentID;

    // Behavior Tree to run for this NPC
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI")
    UBehaviorTree* BehaviorTreeAsset;

    // --- Vision Config (Applied to AI Controller) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightRadius = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float LoseSightRadius = 3500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightAngle = 60.0f; // Half-angle (e.g. 60 = 120 degree FOV)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Hearing")
    float HearingRange = 3000.0f; // 30m hearing range

    // Defines how to handle an action (Switch logic)
    // Hybrid: C++ parses params -> Calls BP Event
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    virtual void ProcessAction(const FGameAction& Action);

    /**
     * Clears physical state (velocity, animation overlay, specific variables) 
     * when a policy is aborted or expires.
     */
    virtual void ClearPhysicalState();

    // --- Reflex & Interrupt System ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Stats")
    FCharacterAttributes CurrentStats;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|AI")
    FString CurrentActionID;

    // Checks Agility vs Difficulty. Returns true if successful.
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    bool TryReflexAction(float Difficulty);

    // Stops current LLM action (move, speak) immediately.
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void AbortCurrentAction();

    /**
     * Emergency Interrupt:
     * 1. Abort current action.
     * 2. Send "Emergency" signal to Cognitive Engine with context.
     */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RequestEmergencyCognition(FString EventType, FString Description);

    // Hook for damage (Override in BP or C++)
    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

    /**
     * Apply movement speeds from CurrentStats to CharacterMovementComponent.
     * Call this after modifying CurrentStats.BaseStats.Dexterity or after RecalculateCombatStats().
     */
    UFUNCTION(BlueprintCallable, Category = "MCP|Stats")
    void ApplyMovementSpeed();

    /**
     * Recalculate all derived stats and apply them.
     * Call this when base stats change.
     */
    UFUNCTION(BlueprintCallable, Category = "MCP|Stats")
    void RefreshStats();


    /**
     * Executes a batch of actions including Behavior Mode and Facial State.
     * Replaces ProcessAction for the new architecture.
     */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    virtual void ExecuteActionBatch(const FActionBatch& Batch);

    // --- Blueprint Implementable Events (Engine Logic) ---
    // Moved to public so BTTasks can call them

    // Move to location with speed
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteMove(FVector TargetLocation, float Speed);

    // Speak text
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteSpeak(const FString& Text);

    // Emote
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteEmote(const FString& EmoteName);

    // Attack
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteAttack(const FString& TargetID);

    // Interact
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteInteract(const FString& TargetID);

    // Generic fallback or other actions (Attack, Interact)
    UFUNCTION(BlueprintImplementableEvent, Category = "MCP|AI")
    void ExecuteGenericAction(const FString& ActionType, const FString& TargetID);

public:
    // --- Debug / Testing ---
    
    /** 
     * Manually trigger an action for testing.
     * Fill in the parameters and click the button in Details panel.
     */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "MCP|Debug")
    void Debug_ExecuteAction(ENPCBehaviorMode Mode, FString ActionName, FString TargetID, FString Content, FString ExtraParamsJson);

    // Preset: Test Social Dialogue
    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Social_Dialogue();

    // Preset: Test Common Move
    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Common_Move();

    // Preset: Test Combat Attack
    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Combat_Attack();

protected:
};
