// Fill out your copyright notice in the Description page of Project Settings.


#include "VRPlayerCharacter.h"
#include "GameplayTagUtils.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "../NPC/SmartNPC.h"
#include "../NPC/NPCManager.h"
#include "PlayerInteractionUtils.h"
#include "PawnDeathUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "../NPC/Struct/NPCActionKeys.h"
#include "GameFramework/PlayerStart.h"
#include "VoiceInputComponent.h"
#include "../Inventory/InventoryComponent.h"
#include "../UI/PlayerHUDWidget.h"
#include "Blueprint/UserWidget.h"

// Sets default values
AVRPlayerCharacter::AVRPlayerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn off this to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

    // AI Perception Stimuli Source
    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    if (StimuliSource)
    {
        // Register this actor as a source for Sight sense
        StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
        StimuliSource->RegisterWithPerceptionSystem();
    }

    // 음성 입력 컴포넌트 — 헤드셋 없이 음성 대화 루프 테스트
    VoiceInput = CreateDefaultSubobject<UVoiceInputComponent>(TEXT("VoiceInput"));

    // 인벤토리 컴포넌트
    Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
}

// Called when the game starts or when spawned
void AVRPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

    // 기본 대기 태그 부여
    AddStateTag(TAG_State_Idle);

    // 음성 입력 — 대상/플레이어 공급자 + transcript 콜백 바인딩 (VRPawn 과 동일)
    if (VoiceInput)
    {
        VoiceInput->ResolveTargetNpc = [this]()
        {
            if (CurrentDialogueTarget.IsEmpty()) DetectNearbyNPC();
            return CurrentDialogueTarget;
        };
        VoiceInput->ResolvePlayerId = [this]() { return GetName(); };
        VoiceInput->OnTranscriptReady.BindUObject(this, &AVRPlayerCharacter::HandleVoiceTranscript);
    }

	// 3. Enhanced Input Subsystem에 IMC 등록
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
            }
        }
    }

    RefreshStats();

    // HUD 생성 — 로컬 플레이어 컨트롤러일 때만
    if (HUDWidgetClass)
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (PC->IsLocalController())
            {
                HUDWidget = CreateWidget<UPlayerHUDWidget>(PC, HUDWidgetClass);
                if (HUDWidget)
                {
                    HUDWidget->AddToViewport();
                }
            }
        }
    }
}

// Called every frame
void AVRPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void AVRPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up Enhanced Input
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Move
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AVRPlayerCharacter::Move);
		}

		// Look
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AVRPlayerCharacter::Look);
		}

		// Jump
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}

		// Fire/Attack
		if (FireAction)
		{
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AVRPlayerCharacter::PerformAttack);
		}

		// Interact — 카메라 조준 라인트레이스로 대화 대상 NPC 지정 (마우스 조준)
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AVRPlayerCharacter::DetectNPCByAim);
		}

		// Voice push-to-talk: 누름 시작 → 녹음, 뗌/취소 → 종료
		if (VoiceInputAction)
		{
			EnhancedInputComponent->BindAction(VoiceInputAction, ETriggerEvent::Started,   this, &AVRPlayerCharacter::OnVoiceStart);
			EnhancedInputComponent->BindAction(VoiceInputAction, ETriggerEvent::Completed, this, &AVRPlayerCharacter::OnVoiceStop);
			EnhancedInputComponent->BindAction(VoiceInputAction, ETriggerEvent::Canceled,  this, &AVRPlayerCharacter::OnVoiceStop);
		}
	}
}

void AVRPlayerCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);

        // 이동 태그 부여 (Idle 제거 및 Run 부여)
        RemoveStateTag(TAG_State_Idle);
        AddStateTag(TAG_State_Action_Common_Move);
	}
}

void AVRPlayerCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(-LookAxisVector.Y);
	}
}

void AVRPlayerCharacter::DetectNearbyNPC()
{
	const FString FoundNPCID = PlayerInteractionUtils::FindNearestNPCId(this, 500.0f);

	// 미발견 시 기존 타겟 유지 — 빈 값으로 덮어쓰면 조준/탐지 한 번 빗나간 것만으로
	// 유효하던 대화 대상이 소실되어 다음 음성 발화가 폐기된다.
	if (!FoundNPCID.IsEmpty())
	{
		CurrentDialogueTarget = FoundNPCID;
		UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] NPC 발견: %s"), *FoundNPCID);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] 주변에 대화할 NPC가 없습니다. (기존 타겟 유지: %s)"), *CurrentDialogueTarget);
	}
}

void AVRPlayerCharacter::DetectNPCByAim()
{
	if (!GetController()) return;

	// 카메라 시점 기준 정면 라인트레이스 — 플랫스크린 마우스 조준으로 대화 대상 선택
	FVector CameraLocation;
	FRotator CameraRotation;
	GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	const FVector Start = CameraLocation;
	const FVector End   = Start + CameraRotation.Vector() * 3000.f;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params))
	{
		if (ASmartNPC* NPC = Cast<ASmartNPC>(Hit.GetActor()))
		{
			CurrentDialogueTarget = NPC->AgentID;
			UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] 조준 NPC 지정: %s"), *CurrentDialogueTarget);
			DrawDebugLine(GetWorld(), Start, Hit.Location, FColor::Cyan, false, 1.0f, 0, 2.0f);
			return;
		}
	}

	// 조준 빗나감 — 근접 탐지로 폴백
	UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] 조준에 NPC 없음 — 근접 탐지 폴백"));
	DetectNearbyNPC();
}

void AVRPlayerCharacter::OnVoiceStart(const FInputActionValue& Value)
{
	if (VoiceInput) VoiceInput->StartTalking();
}

void AVRPlayerCharacter::OnVoiceStop(const FInputActionValue& Value)
{
	if (VoiceInput) VoiceInput->StopTalking();
}

void AVRPlayerCharacter::HandleVoiceTranscript(const FString& PlayerId, const FString& TargetNpc, const FString& Transcript)
{
	// ASR transcript → 기존 단순 대화 경로 재사용. 대상은 ASR echo 우선, 없으면 현재 타겟.
	const FString Target = TargetNpc.IsEmpty() ? CurrentDialogueTarget : TargetNpc;
	if (Target.IsEmpty() || Transcript.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[VRPlayerCharacter] Voice transcript 폐기 — target/transcript 비어있음"));
		return;
	}
	PlayerInteractionUtils::SendDialogueToNpc(this, PlayerId.IsEmpty() ? GetName() : PlayerId, Target, Transcript);
}

void AVRPlayerCharacter::SendNPCDialogue(const FString& Text)
{
	// 대상 미지정 시 1회 재탐지
	if (CurrentDialogueTarget.IsEmpty())
	{
		DetectNearbyNPC();
	}
	if (CurrentDialogueTarget.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[VRPlayerCharacter] SendNPCDialogue 실패 — 대상 NPC 없음"));
		return;
	}
	// player_id = actor 이름 — affinity DB 키(예: BP_Player_C_0)와 일치
	PlayerInteractionUtils::SendDialogueToNpc(this, GetName(), CurrentDialogueTarget, Text);
}

void AVRPlayerCharacter::TestPlanHUD(const FString& AgentID)
{
	FString Target = AgentID;
	if (Target.IsEmpty())
	{
		if (CurrentDialogueTarget.IsEmpty()) { DetectNearbyNPC(); }
		Target = CurrentDialogueTarget;
	}
	if (Target.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlanHUD] TestPlanHUD 실패 — 대상 NPC 없음"));
		return;
	}

	UNPCManager* Manager = UNPCManager::Get(this);
	ASmartNPC* NPC = Manager ? Manager->GetNPCById(Target) : nullptr;
	if (!NPC || !NPC->GetStateComponent())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlanHUD] TestPlanHUD 실패 — NPC '%s' 미발견(PIE 미등록?)"), *Target);
		return;
	}

	FNPCPlan Test;
	Test.Goal = TEXT("DEBUG TEST PLAN");
	Test.Steps = { TEXT("step1"), TEXT("step2") };
	NPC->GetStateComponent()->SetCurrentPlan(Test); // → Broadcast → HandlePlanUpdated → HUD (PIE 인스턴스)
	UE_LOG(LogTemp, Warning, TEXT("[PlanHUD] TestPlanHUD 주입 완료 — %s"), *Target);
}

void AVRPlayerCharacter::PerformAttack()
{
	if (!GetController()) return;

    // 공격 태그 부여
    RemoveStateTag(TAG_State_Idle);
    AddStateTag(TAG_State_Action_Combat_Attack);
    
    // 공격 소음 발생
    UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), 1.0f, this, 0.0f, NPCActionKeys::NoiseTag_Attack);

	// Get camera location and forward direction
	FVector CameraLocation;
	FRotator CameraRotation;
	GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	FVector Start = CameraLocation;
	FVector ForwardVector = CameraRotation.Vector();
	FVector End = Start + (ForwardVector * AttackRange);

	// Line Trace
	FHitResult HitResult;
	FCollisionQueryParams CollisionParams;
	CollisionParams.AddIgnoredActor(this);

	bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		Start,
		End,
		ECC_Pawn, // Or ECC_Visibility
		CollisionParams
	);

	if (bHit && HitResult.GetActor())
	{
		AActor* HitActor = HitResult.GetActor();
		UE_LOG(LogTemp, Verbose, TEXT("Player Attack Hit: %s"), *HitActor->GetName());

		// Apply Damage
		FPointDamageEvent DamageEvent;
		DamageEvent.HitInfo = HitResult;
		DamageEvent.ShotDirection = ForwardVector;
		DamageEvent.DamageTypeClass = UDamageType::StaticClass();

		HitActor->TakeDamage(AttackDamage, DamageEvent, GetController(), this);

		// Optional: Visual feedback (draw debug line)
		DrawDebugLine(GetWorld(), Start, HitResult.Location, FColor::Red, false, 1.0f, 0, 2.0f);
	}
	else
	{
		// Miss - draw debug line to show attack direction
		DrawDebugLine(GetWorld(), Start, End, FColor::Green, false, 0.5f, 0, 1.0f);
	}

    // 몽타주 재생 → 종료 델리게이트에서 태그 회수
    if (AttackMontage)
    {
        UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
        if (AnimInstance)
        {
            AnimInstance->Montage_Play(AttackMontage);

            FOnMontageEnded EndDelegate;
            EndDelegate.BindUObject(this, &AVRPlayerCharacter::OnAttackMontageEnded);
            AnimInstance->Montage_SetEndDelegate(EndDelegate, AttackMontage);
        }
    }
    else
    {
        // 몽타주 미할당 시 즉시 회수
        RemoveStateTag(TAG_State_Action_Combat_Attack);
        AddStateTag(TAG_State_Idle);
    }
}

void AVRPlayerCharacter::ApplyMovementSpeed()
{
	UCharacterMovementComponent* MovementComp = GetCharacterMovement();
	if (!MovementComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VRPlayerCharacter] No CharacterMovementComponent found!"));
		return;
	}

	// Apply calculated speeds from PlayerAttributes
	MovementComp->MaxWalkSpeed = CurrentStats.Movement.WalkSpeed;
	MovementComp->MaxWalkSpeedCrouched = CurrentStats.Movement.CrouchSpeed;
	
	UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] Applied Movement Speeds - Walk: %.1f, Crouch: %.1f (Dex: %d)"),
		CurrentStats.Movement.WalkSpeed,
		CurrentStats.Movement.CrouchSpeed,
		CurrentStats.BaseStats.Dexterity);
}

void AVRPlayerCharacter::RefreshStats()
{
	// Recalculate all derived stats from base stats
	CurrentStats.RecalculateCombatStats();
	
	// Apply movement speeds to CharacterMovementComponent
	ApplyMovementSpeed();
	
	// Update AttackDamage from Combat stats if desired (optional)
	// AttackDamage = CurrentStats.Combat.AttackPower;
	
	UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] Stats Refreshed - HP: %.1f/%.1f, Walk: %.1f, Run: %.1f, Sprint: %.1f"),
		CurrentStats.Resources.Health,
		CurrentStats.Resources.MaxHealth,
		CurrentStats.Movement.WalkSpeed,
		CurrentStats.Movement.RunSpeed,
		CurrentStats.Movement.SprintSpeed);
}

float AVRPlayerCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	
	// Apply damage to Resources.Health
	CurrentStats.Resources.Health -= ActualDamage;
	if (CurrentStats.Resources.Health < 0) CurrentStats.Resources.Health = 0;

	UE_LOG(LogTemp, Warning, TEXT("[VRPlayerCharacter] Took Damage: %.1f. HP: %.1f/%.1f"), 
		ActualDamage, CurrentStats.Resources.Health, CurrentStats.Resources.MaxHealth);

	// Check for death
	if (CurrentStats.Resources.Health <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[VRPlayerCharacter] PLAYER DIED!"));
		HandleDeath();  // Dead 태그 세팅은 PawnDeathUtils::HandleDeath 내부에서 일괄
	}

	return ActualDamage;
}

// --- IGameplayTagAssetInterface 구현 ---

void AVRPlayerCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    TagContainer = GameplayTags;
}

void AVRPlayerCharacter::AddStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::AddState(GameplayTags, Tag);
}

void AVRPlayerCharacter::RemoveStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::RemoveState(GameplayTags, Tag);
}

void AVRPlayerCharacter::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    RemoveStateTag(TAG_State_Action_Combat_Attack);
    AddStateTag(TAG_State_Idle);
}

void AVRPlayerCharacter::SaveCheckpoint(const FVector& Location, const FRotator& Rotation)
{
    PawnDeathUtils::SaveCheckpoint(CurrentStats, /*bRequireAlive*/true,
        Location, Rotation, bHasCheckpoint, CheckpointLocation,
        CheckpointRotation, CheckpointHP, TEXT("VRPlayerCharacter"));
}

void AVRPlayerCharacter::HandleDeath()
{
    PawnDeathUtils::HandleDeath(this, GameplayTags, /*bClearCursor*/true,
        RespawnDelay, RespawnTimerHandle,
        FTimerDelegate::CreateUObject(this, &AVRPlayerCharacter::Respawn), TEXT("VRPlayerCharacter"));
}

void AVRPlayerCharacter::Respawn()
{
    PawnDeathUtils::Respawn(this, CurrentStats, bHasCheckpoint, CheckpointLocation,
        CheckpointRotation, CheckpointHP, GameplayTags, TEXT("VRPlayerCharacter"));
}
