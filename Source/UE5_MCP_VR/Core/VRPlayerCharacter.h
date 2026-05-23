#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "../Core/Entity.h"          // IPlayerEntity → ICharacterEntity → IGameplayTagAssetInterface 포함
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "NativeGameplayTags.h"
#include "PlayerGameplayTags.h"
#include "VRPlayerCharacter.generated.h"

UCLASS()
class UE5_MCP_VR_API AVRPlayerCharacter : public ACharacter, public IPlayerBase
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

	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	void AddStateTag(FGameplayTag Tag);
	void RemoveStateTag(FGameplayTag Tag);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer GameplayTags;

	// Player Stats (FPlayerAttributes: AI BehavioralTraits 없이 플레이어 전용 스탯만 보유)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FPlayerAttributes CurrentStats;

    // === IEntity / IPlayerEntity 인터페이스 구현 ===
    virtual FString GetEntityID_Implementation() const override { return GetName(); }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::Player; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }

    // ICharacterBase
    virtual FCharacterAttributesBase GetAttributes_Implementation() const override { return CurrentStats; }
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const override { return false; }

    // IPlayerBase
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

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputMappingContext* DefaultMappingContext;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* JumpAction;

	/** Interact Input Action (대화 대상 NPC 지정) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* InteractAction;

	/** Fire/Attack Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* FireAction;
	
	// --- Movement ---
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	// --- Combat ---
	void PerformAttack();

	UFUNCTION()
	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// AttackDamage is now derived from CurrentStats.Combat.AttackPower, but can be overridden
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float AttackDamage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float AttackRange = 3000.0f; // 30m

	// 공격 애니메이션 몽타주 — BP_Player에서 할당. 미할당 시 즉시 태그 회수 폴백.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	UAnimMontage* AttackMontage = nullptr;

	
	// --- Interaction ---
	void DetectNearbyNPC();

	// --- Death / Respawn ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	float RespawnDelay = 5.0f;

	// 체크포인트 액터가 호출 — 현재 위치/HP를 저장
	void SaveCheckpoint(const FVector& Location, const FRotator& Rotation);

	void HandleDeath();

private:
	void Respawn();

	// 마지막으로 저장된 체크포인트 데이터 (체크포인트 미도달 시 IsSet=false)
	bool bHasCheckpoint = false;
	FVector CheckpointLocation;
	FRotator CheckpointRotation;
	float CheckpointHP = 0.f;

	FTimerHandle RespawnTimerHandle;
};
