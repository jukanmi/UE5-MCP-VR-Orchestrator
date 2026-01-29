// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "../Network/WebSocketClient.h"
#include "../UI/ChatWidget.h"
#include "VRPlayerCharacter.generated.h"

UCLASS()
class UE5_MCP_VR_API AVRPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AVRPlayerCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

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
	
	// --- Interaction ---
	void DetectNearbyNPC();
	FString CurrentTargetID;

};
