// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "../Network/WebSocketClient.h"
#include "ChatWidget.generated.h"

/**
 * 
 */
UCLASS()
class UE5_MCP_VR_API UChatWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	virtual void NativeConstruct() override;

	// UI Components
	UPROPERTY(meta = (BindWidget))
	UScrollBox* ChatHistoryScrollBox;

	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* InputTextBox;

	// Input Handling
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void SendChatMessage();

	UFUNCTION()
	void OnInputTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// Display
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void AddMessageToHistory(const FString& Sender, const FString& Message);

	// Target NPC
	UPROPERTY(BlueprintReadWrite, Category = "Chat")
	FString CurrentTargetNPCID;

	UFUNCTION()
	void OnNPCResponseReceived(const FString& NPCName, const FString& Message);

	// 대화창 닫기
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void CloseChat();
};
