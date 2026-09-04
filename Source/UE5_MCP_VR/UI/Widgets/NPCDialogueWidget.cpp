#include "UI/Widgets/NPCDialogueWidget.h"

#include "Components/TextBlock.h"

void UNPCDialogueWidget::SetDialogue(const FString& Speaker, const FString& Text)
{
    if (SpeakerName)
    {
        SpeakerName->SetText(FText::FromString(Speaker));
    }
    if (DialogueText)
    {
        DialogueText->SetText(FText::FromString(Text));
    }

    OnDialogueSet(Speaker, Text);
}
