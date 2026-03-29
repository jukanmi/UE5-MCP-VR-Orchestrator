// Fill out your copyright notice in the Description page of Project Settings.


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
