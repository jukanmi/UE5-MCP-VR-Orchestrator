#include "UI/Widgets/NPCDialogueWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

TSharedRef<SWidget> UNPCDialogueWidget::RebuildWidget()
{
    // 디자이너 트리가 없을 때만 코드로 짓는다. 이 클래스를 부모로 하는 WBP 를 만들면
    // 그쪽 트리가 그대로 쓰이고 여기는 건너뛴다.
    //
    // 코드로 짓는 이유: 이 위젯은 지금까지 WBP 없이 C++ 클래스가 그대로 위젯 클래스로
    // 지정돼 있었다. 그 상태에서는 BindWidgetOptional 인 두 텍스트가 항상 null 이라
    // SetDialogue 가 조용히 아무것도 하지 않았고, 말풍선이 빈 채로 떠 있었다.
    // WidgetTree 는 위젯 블루프린트가 초기화할 때만 채워진다. WBP 없이 이 C++ 클래스를
    // 그대로 위젯 클래스로 쓰면 null 이라, 없으면 여기서 직접 만든다.
    // (2026-09-06 실측: 이 가드가 없어 트리가 안 지어지고 빈 위젯만 떠 있었다.)
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    }

    if (!WidgetTree->RootWidget)
    {
        UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogueFrame"));
        Frame->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.65f));
        Frame->SetPadding(FMargin(16.f, 10.f));

        UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogueBox"));
        Frame->SetContent(Box);

        SpeakerName = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpeakerName"));
        DialogueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogueText"));

        // 월드 공간 위젯이라 기본 폰트 크기로는 몇 미터만 떨어져도 못 읽는다.
        // 폰트 자체는 슬레이트 기본을 쓴다 — 한글은 이 경로에서 폰트 폴백으로 그려진다.
        FSlateFontInfo Font = SpeakerName->GetFont();
        Font.Size = 26;
        SpeakerName->SetFont(Font);
        SpeakerName->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.85f, 1.f, 1.f)));

        Font.Size = 30;
        DialogueText->SetFont(Font);
        DialogueText->SetAutoWrapText(true);

        Box->AddChildToVerticalBox(SpeakerName);
        Box->AddChildToVerticalBox(DialogueText);

        WidgetTree->RootWidget = Frame;
    }

    return Super::RebuildWidget();
}

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
