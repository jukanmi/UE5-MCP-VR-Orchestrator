#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "../Core/Entity.h"          // IPlayerEntity → ICharacterEntity → IGameplayTagAssetInterface 포함
#include "../UI/ChatWidget.h"
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "VRPlayerCharacter.generated.h"

UCLASS()
class UE5_MCP_VR_API AVRPlayerCharacter : public ACharacter, public IPlayerEntity
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

	// --- IGameplayTagAssetInterface 구현 ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer GameplayTags;

	// Player Stats (FPlayerAttributes: AI BehavioralTraits 없이 플레이어 전용 스탯만 보유)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FPlayerAttributes CurrentStats;

    // === IEntity / IPlayerEntity 인터페이스 구현 ===
    virtual FString GetEntityID_Implementation() const override { return GetName(); }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::Player; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }

    // ICharacterEntity
    virtual FCharacterAttributesBase GetAttributes_Implementation() const override { return CurrentStats; }
    virtual bool IsAlive_Implementation() const override { return CurrentStats.Resources.IsAlive(); }
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterEntity>& Other) const override { return false; }

    // IPlayerEntity
    virtual FString GetPlayerName_Implementation() const override { return GetName(); }
    virtual FPlayerAttributes GetPlayerAttributes_Implementation() const override { return CurrentStats; }

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
