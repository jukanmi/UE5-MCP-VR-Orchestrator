// Fill out your copyright notice in the Description page of Project Settings.


#include "VRPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "../NPC/SmartNPC.h"
#include "Engine/OverlapResult.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "../NPC/Struct/NPCActionKeys.h"
#include "GameFramework/PlayerStart.h"

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
        StimuliSource->RegisterForSense(TSubclassOf<UAISense_Sight>());
        StimuliSource->RegisterWithPerceptionSystem();
    }
}

// Called when the game starts or when spawned
void AVRPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	if (ChatWidgetClass)
	{
		ChatWidgetInstance = CreateWidget<UChatWidget>(GetWorld(), ChatWidgetClass);
		if (ChatWidgetInstance)
		{
			ChatWidgetInstance->AddToViewport();
			ChatWidgetInstance->SetVisibility(ESlateVisibility::Hidden);
		}
	}

    // 기본 대기 태그 부여
    AddStateTag(TAG_State_Idle);

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
		if (ToggleChatAction)
		{
			EnhancedInputComponent->BindAction(ToggleChatAction, ETriggerEvent::Started, this, &AVRPlayerCharacter::ToggleChat);
		}

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

		// Interact
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AVRPlayerCharacter::DetectNearbyNPC);
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

void AVRPlayerCharacter::ToggleChat()
{
	if (!ChatWidgetInstance) return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	if (ChatWidgetInstance->GetVisibility() == ESlateVisibility::Visible)
	{
		// Close
		ChatWidgetInstance->SetVisibility(ESlateVisibility::Hidden);
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
	else
	{
		// Open
		ChatWidgetInstance->SetVisibility(ESlateVisibility::Visible);
		PC->bShowMouseCursor = true;
		
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(ChatWidgetInstance->GetCachedWidget());
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

		PC->SetInputMode(InputMode);
	}
}

void AVRPlayerCharacter::DetectNearbyNPC()
{
	// 이미 대화 중이면 닫기
	if (ChatWidgetInstance && ChatWidgetInstance->GetVisibility() == ESlateVisibility::Visible)
	{
		ChatWidgetInstance->CurrentTargetNPCID = TEXT("");
		CurrentTargetID = TEXT("");
		ToggleChat();
		return;
	}

	// SimpleSphere Trace or Overlap
	FVector Start = GetActorLocation();
	float Radius = 500.0f; 

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	bool bHit = GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		Start,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECollisionChannel::ECC_Pawn),
		FCollisionShape::MakeSphere(Radius),
		Params
	);

	FString FoundNPCID = "";
	float MinDistSq = FLT_MAX;

	if (bHit)
	{
		for (const FOverlapResult& Result : Overlaps)
		{
			ASmartNPC* NPC = Cast<ASmartNPC>(Result.GetActor());
			if (NPC)
			{
				float DistSq = FVector::DistSquared(Start, NPC->GetActorLocation());
				if (DistSq < MinDistSq)
				{
					MinDistSq = DistSq;
					FoundNPCID = NPC->AgentID;
				}
			}
		}
	}

	if (bHit && FoundNPCID != "")
	{
		CurrentTargetID = FoundNPCID;
		UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] NPC 발견: %s. 대화를 시작합니다."), *CurrentTargetID);
		
		// Update UI with target
		if (ChatWidgetInstance)
		{
			ChatWidgetInstance->CurrentTargetNPCID = CurrentTargetID;
			
			// UI 강제 열기
			if (ChatWidgetInstance->GetVisibility() != ESlateVisibility::Visible)
			{
				ToggleChat(); // ToggleChat에서 마우스 모드 전환까지 처리해줌
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] 주변에 대화할 NPC가 없습니다."));
	}
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
        RemoveStateTag(TAG_State_Idle);
        AddStateTag(TAG_State_Condition_Dead);
		HandleDeath();
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
    if (Tag.IsValid())
    {
        GameplayTags.AddTag(Tag);
    }
}

void AVRPlayerCharacter::RemoveStateTag(FGameplayTag Tag)
{
    if (Tag.IsValid() && GameplayTags.HasTagExact(Tag))
    {
        GameplayTags.RemoveTag(Tag);
    }
}

void AVRPlayerCharacter::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    RemoveStateTag(TAG_State_Action_Combat_Attack);
    AddStateTag(TAG_State_Idle);
}

void AVRPlayerCharacter::SaveCheckpoint(const FVector& Location, const FRotator& Rotation)
{
    if (!CurrentStats.Resources.IsAlive()) return;

    bHasCheckpoint    = true;
    CheckpointLocation = Location;
    CheckpointRotation = Rotation;
    CheckpointHP       = CurrentStats.Resources.Health;

    UE_LOG(LogTemp, Log, TEXT("[Checkpoint] 저장 — 위치: %s, HP: %.1f"), *Location.ToString(), CheckpointHP);
}

void AVRPlayerCharacter::HandleDeath()
{
    // 입력 차단
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        DisableInput(PC);
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly());
    }

    // 채팅창 닫기
    if (ChatWidgetInstance && ChatWidgetInstance->GetVisibility() == ESlateVisibility::Visible)
    {
        ChatWidgetInstance->SetVisibility(ESlateVisibility::Hidden);
    }

    // 충돌/메시 비활성화
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetVisibility(false);

    UE_LOG(LogTemp, Warning, TEXT("[VRPlayerCharacter] 사망 — %.1f초 후 리스폰"), RespawnDelay);

    GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &AVRPlayerCharacter::Respawn, RespawnDelay, false);
}

void AVRPlayerCharacter::Respawn()
{
    if (bHasCheckpoint)
    {
        SetActorLocationAndRotation(CheckpointLocation, CheckpointRotation);
        CurrentStats.Resources.Health = CheckpointHP;
    }
    else
    {
        // 체크포인트 미도달 시 PlayerStart 폴백
        AActor* StartPoint = UGameplayStatics::GetActorOfClass(GetWorld(), APlayerStart::StaticClass());
        if (StartPoint)
        {
            SetActorLocationAndRotation(StartPoint->GetActorLocation(), StartPoint->GetActorRotation());
        }
        CurrentStats.Resources.Health = CurrentStats.Resources.MaxHealth;
    }

    // 충돌/메시 복구
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetMesh()->SetVisibility(true);

    // 태그 초기화
    RemoveStateTag(TAG_State_Condition_Dead);
    AddStateTag(TAG_State_Idle);

    // 입력 복구
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        EnableInput(PC);
    }

    UE_LOG(LogTemp, Log, TEXT("[VRPlayerCharacter] 리스폰 완료 — HP: %.1f"), CurrentStats.Resources.Health);
}
