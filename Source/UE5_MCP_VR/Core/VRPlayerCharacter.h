// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "../AI/CharacterAttributes.h"
#include "../Network/WebSocketClient.h"
#include "../UI/ChatWidget.h"
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "VRPlayerCharacter.generated.h"

UCLASS()
class UE5_MCP_VR_API AVRPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AVRPlayerCharacter();

    /** AI Perception Stimuli Source */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UAIPerceptionStimuliSourceComponent* StimuliSource;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Player Stats (FPlayerAttributes: no behavioral traits, UI controls instead of AI)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FPlayerAttributes CurrentStats;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// --- Stats Management ---
	
	/** Apply movement speeds from CurrentStats to CharacterMovementComponent. */
	UFUNCTION(BlueprintCallable, Category = "Stats")
	void ApplyMovementSpeed();

	/** Recalculate all derived stats and apply them. Call when base stats change. */
	UFUNCTION(BlueprintCallable, Category = "Stats")
	void RefreshStats();

	// Override TakeDamage to apply to Resources.Health
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// --- Network ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP Network")
	UWebSocketClient* WebSocketClient;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP Network")
	FString WebSocketURL = "ws://127.0.0.1:8000/ws/ue5";

	UFUNCTION()
	void OnWebSocketMessage(const FString& Message);

	// --- UI ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UChatWidget> ChatWidgetClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	UChatWidget* ChatWidgetInstance;

	// Input Actions
	void ToggleChat();

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputMappingContext* DefaultMappingContext;

	/** Toggle Chat Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* ToggleChatAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* JumpAction;

	/** Fire/Attack Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* FireAction;
	
	// --- Movement ---
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	// --- Combat ---
	void PerformAttack();

	// AttackDamage is now derived from CurrentStats.Combat.AttackPower, but can be overridden
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float AttackDamage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float AttackRange = 3000.0f; // 30m

	
	// --- Interaction ---
	void DetectNearbyNPC();
	FString CurrentTargetID;

};
