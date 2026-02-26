// Fill out your copyright notice in the Description page of Project Settings.


#include "ChatWidget.h"
#include "Components/TextBlock.h"
#include "Perception/AISense_Hearing.h"
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
	JsonObject->SetStringField("player_id", "Player_1");
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

	// Add Player Location for "come here" type commands
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			FVector PlayerLoc = Pawn->GetActorLocation();
			TSharedPtr<FJsonObject> PlayerLocObject = MakeShareable(new FJsonObject);
			PlayerLocObject->SetNumberField("x", PlayerLoc.X);
			PlayerLocObject->SetNumberField("y", PlayerLoc.Y);
			PlayerLocObject->SetNumberField("z", PlayerLoc.Z);
			JsonObject->SetObjectField("player_location", PlayerLocObject);

			// --- [NEW] Trigger engine noise event for AI Hearing ---
			// Loudness 1.0 (Normal speech), Range is controlled by NPC's HearingRange
			UAISense_Hearing::ReportNoiseEvent(GetWorld(), PlayerLoc, 1.0f, Pawn, 0.0f);
			UE_LOG(LogTemp, Log, TEXT("[ChatWidget] Reported Speech Noise at %s"), *PlayerLoc.ToString());
		}
	}

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// TODO: [UE5] Python 서버가 새로운 MessageEnvelope 구조를 요구하므로, 
	// FEnvelopeBuilder::BuildPrompt(JsonString)를 사용하여 완성된 문자열을 얻은 후 전송해야 합니다.
	// 예시: FString EnvelopeJson = FEnvelopeBuilder::BuildPrompt(JsonString);
	//       WebSocketClient->SendPrompt(EnvelopeJson); // SendPrompt 내부 구현도 범용으로 수정 필요

	// Send via WebSocket
	if (WebSocketClient)
	{
		WebSocketClient->SendPrompt(JsonString);
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
