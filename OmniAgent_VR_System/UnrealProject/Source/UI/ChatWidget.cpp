// Fill out your copyright notice in the Description page of Project Settings.


#include "ChatWidget.h"
#include "Components/TextBlock.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

void UChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (InputTextBox)
	{
		InputTextBox->OnTextCommitted.AddDynamic(this, &UChatWidget::OnInputTextCommitted);
	}
}

void UChatWidget::SendChatMessage()
{
	if (!InputTextBox || InputTextBox->GetText().IsEmpty())
	{
		return;
	}

	FString MessageText = InputTextBox->GetText().ToString();
	
	// Create GesPrompt JSON
	// Structure matches: OmniAgent_VR_System/CognitiveEngine/app/schemas/vr_context.py
	
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField("player_id", "Player_1"); // Hardcoded for now
	JsonObject->SetStringField("voice_transcript", MessageText);
	JsonObject->SetNumberField("timestamp", FDateTime::UtcNow().ToUnixTimestamp());
	
	// Gestures (Empty list for text chat)
	TArray<TSharedPtr<FJsonValue>> GesturesArray;
	JsonObject->SetArrayField("gestures", GesturesArray);

	// Context (Looking at)
	if (!CurrentTargetNPCID.IsEmpty())
	{
		JsonObject->SetStringField("looking_at_entity_id", CurrentTargetNPCID);
	}

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// Send via WebSocket
	if (WebSocketClient)
	{
		WebSocketClient->SendData(JsonString);
		AddMessageToHistory("Player", MessageText); // Show own message
	}
	else
	{
		AddMessageToHistory("System", "Error: WebSocket Disconnected");
	}

	// Clear Input
	InputTextBox->SetText(FText::GetEmpty());
}

void UChatWidget::OnInputTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SendChatMessage();
	}
}

void UChatWidget::AddMessageToHistory(const FString& Sender, const FString& Message)
{
	if (!ChatHistoryScrollBox) return;

	UTextBlock* NewMessageBlock = NewObject<UTextBlock>(this);
	if (NewMessageBlock)
	{
		FString FormattedMessage = FString::Printf(TEXT("%s: %s"), *Sender, *Message);
		NewMessageBlock->SetText(FText::FromString(FormattedMessage));
		
		// Optional: Style the text
		FSlateFontInfo FontInfo = NewMessageBlock->GetFont();
		FontInfo.Size = 14;
		NewMessageBlock->SetFont(FontInfo);

		if (Sender == "Player")
		{
			NewMessageBlock->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
		}
		else if (Sender == "System")
		{
			NewMessageBlock->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else
		{
			NewMessageBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}

		ChatHistoryScrollBox->AddChild(NewMessageBlock);
		ChatHistoryScrollBox->ScrollToEnd();
	}
}
