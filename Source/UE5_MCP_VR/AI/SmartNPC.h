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

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // Unique ID for routing (e.g. "Guard_1")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI")
    FString AgentID;

    // Movement Types for speed control
    UENUM(BlueprintType)
    enum class EMoveType : uint8
    {
        Walk,
        Run,
        Sprint,
        Crouch
    };

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

    // --- Facial State ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|AI|Facial")
    EFacialState CurrentFacialState = EFacialState::Neutral;

    // --- Action Queue System ---
    TQueue<FGameAction> ActionQueue;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "MCP|AI|Queue")
    bool bIsBusy = false;

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void ProcessNextAction();

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void OnActionCompleted();

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void StopAllActions();

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    bool TryReflexAction(int Difficulty);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void AbortCurrentAction();

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RequestEmergencyCognition(FString EventType, FString Description);

    // Hook for damage (Override in BP or C++)
    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

    UFUNCTION(BlueprintCallable, Category = "MCP|Stats")
    void ApplyMovementSpeed();

    UFUNCTION(BlueprintCallable, Category = "MCP|Stats")
    void RefreshStats();

    // --- BTTask Action ---

    // 1. Movement
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteMoveToLocation(FVector TargetLocation, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

    // 2. Keep Distance
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteKeepDistance(AActor* TargetActor, float Distance, float Speed = 300.f);

    // 3. Wait (Custom idle/wait behavior)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteWait(float Duration);

    // 4. Dialogue (Rich dialogue with emotion/metadata)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteDialogue(const FString& DialogueText, const FString& EmotionID);

    // 5. Rotate Body
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteFaceRotate(FVector TargetLocation, float TurnSpeed = 5.f);

    // 6. Attack (Specific attack type)
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecutePerformAttack(AActor* TargetActor, const FString& AttackType);

    // 7. Defend
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteDefend(bool bStartDefend);

    // 8. Roll/Dodge
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteRoll();

    // 9. Hand Signal
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteHandSignal(const FString& SignalName);

    // 10. Emote
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    virtual void ExecuteEmote(const FString& EmoteName);

protected:
    // --- Helper implementation for Action Batch ---
    virtual void UpdateBehaviorState(const struct FActionBatch& Batch);
    virtual void DispatchActions(const TArray<FGameAction>& Actions);
    
    // Helper to determine behavior mode from action string
    FString GetBehaviorModeFromAction(const FString& ActionType) const;
    
    // --- Original Protected Section ---


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

    // Integration Test: Simulate Full Pipeline
    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Orchestra_Pipeline();

protected:
};
