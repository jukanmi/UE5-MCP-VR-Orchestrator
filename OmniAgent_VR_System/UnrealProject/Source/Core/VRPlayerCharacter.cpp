// Fill out your copyright notice in the Description page of Project Settings.


#include "VRPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "../AI/SmartNPC.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Engine/OverlapResult.h"

// Sets default values
AVRPlayerCharacter::AVRPlayerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AVRPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	// 1. Initialize WebSocket
	WebSocketClient = NewObject<UWebSocketClient>(this);
	if (WebSocketClient)
	{
		WebSocketClient->OnMessageReceived.AddDynamic(this, &AVRPlayerCharacter::OnWebSocketMessage);
		WebSocketClient->Initialize(WebSocketURL);
	}

	// 2. Initialize UI (Hidden by default)
	if (ChatWidgetClass)
	{
		ChatWidgetInstance = CreateWidget<UChatWidget>(GetWorld(), ChatWidgetClass);
		if (ChatWidgetInstance)
		{
			ChatWidgetInstance->AddToViewport();
			ChatWidgetInstance->SetVisibility(ESlateVisibility::Hidden);
			ChatWidgetInstance->WebSocketClient = WebSocketClient; // Pass WS reference
		}
	}

	// 3. Enhanced Input Subsystem에 IMC 등록 (이 부분이 누락됨)
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
}

// Called every frame
void AVRPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	DetectNearbyNPC();
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

	CurrentTargetID = FoundNPCID;
	
	// Update UI with target
	if (ChatWidgetInstance)
	{
		ChatWidgetInstance->CurrentTargetNPCID = CurrentTargetID;
	}
}

void AVRPlayerCharacter::OnWebSocketMessage(const FString& Message)
{
	// Parse ActionBatch to find "Speak" actions
	// We can reuse the ChatWidget's logic or parse here.
	// For robust JSON parsing, let's look for "Speak" action type.
	
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		// Check for "actions" array
		const TArray<TSharedPtr<FJsonValue>>* ActionsArray;
		if (JsonObject->TryGetArrayField(TEXT("actions"), ActionsArray))
		{
			for (const auto& ActionValue : *ActionsArray)
			{
				TSharedPtr<FJsonObject> ActionObj = ActionValue->AsObject();
				if (ActionObj.IsValid())
				{
					FString ActionType = ActionObj->GetStringField(TEXT("action_type"));
					if (ActionType == TEXT("Speak"))
					{
						FString Text = ActionObj->GetStringField(TEXT("text"));
						// Ideally get sender ID too, but defaulting to "NPC" or deriving from context
						FString AgentID = JsonObject->GetStringField(TEXT("agent_id")); // Batch level agent ID
						
						if (ChatWidgetInstance)
						{
							ChatWidgetInstance->AddMessageToHistory(AgentID, Text);
						}
					}
				}
			}
		}
		
		// Also handle direct error messages or generic logging
		if (JsonObject->HasField(TEXT("error")))
		{
			if (ChatWidgetInstance)
			{
				ChatWidgetInstance->AddMessageToHistory(TEXT("System"), JsonObject->GetStringField(TEXT("error")));
			}
		}
	}
}
