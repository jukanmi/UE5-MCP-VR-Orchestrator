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

class UVoiceInputComponent;
class UInventoryComponent;
class UPlayerHUDWidget;

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

    /** 음성 입력 — push-to-talk 마이크 캡처 → ASR → transcript → 대화.
     *  헤드셋 없이 플랫스크린에서 실제 음성 대화 루프를 테스트하기 위해 VRPawn 에서 이식. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ASR")
    UVoiceInputComponent* VoiceInput;

    /** 인벤토리 — 슬롯/장비/무게. 기존 UInventoryComponent 재사용. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
    UInventoryComponent* Inventory;

    /** HUD 위젯 클래스 — BP_VRPlayerCharacter 에서 WBP 지정. 미지정 시 HUD 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

    /** 생성된 HUD 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Category = "UI")
    UPlayerHUDWidget* HUDWidget;

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

	/** Push-to-talk — 누름(Started) StartTalking, 뗌(Completed/Canceled) StopTalking */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* VoiceInputAction;
	
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
	/** 반경 500cm 내 최근접 NPC 를 대화 대상으로 지정 (음성/콘솔 폴백용). */
	void DetectNearbyNPC();

	/** 카메라 정면 라인트레이스로 조준한 NPC 를 대화 대상으로 지정 — 플랫스크린 마우스 조준.
	 *  Interact 입력에 바인딩. 빗나가면 DetectNearbyNPC 로 폴백. */
	void DetectNPCByAim();

	// --- Voice Input (push-to-talk) ---
	void OnVoiceStart(const FInputActionValue& Value);
	void OnVoiceStop(const FInputActionValue& Value);

	/** ASR transcript 확정 → NPCManager::SendPlayerDialogue 로 전달. */
	void HandleVoiceTranscript(const FString& PlayerId, const FString& TargetNpc, const FString& Transcript);

	// --- Dialogue ---
	/** 마지막으로 지정된 대화 대상 AgentID. BP 에서 직접 세팅 가능(BlueprintReadWrite). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FString CurrentDialogueTarget;

	/** 콘솔에서 플레이어 발화를 최근접 NPC로 전송 (단순 대화). 예: SendNPCDialogue "안녕" */
	UFUNCTION(Exec)
	void SendNPCDialogue(const FString& Text);

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
