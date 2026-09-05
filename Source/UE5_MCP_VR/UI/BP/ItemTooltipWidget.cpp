#include "UI/BP/ItemTooltipWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
    // 종류별 이름 색 — 데이터에 희귀도 열이 없어 ItemType 으로 대신한다.
    FLinearColor ColorForType(EItemType Type)
    {
        switch (Type)
        {
        case EItemType::Consumable: return FLinearColor(0.45f, 1.f, 0.45f, 1.f);
        case EItemType::Equipment:  return FLinearColor(0.45f, 0.75f, 1.f, 1.f);
        case EItemType::Quest:      return FLinearColor(1.f, 0.85f, 0.3f, 1.f);
        default:                    return FLinearColor::White;
        }
    }

    FString LabelForType(EItemType Type)
    {
        switch (Type)
        {
        case EItemType::Consumable: return TEXT("소비");
        case EItemType::Equipment:  return TEXT("장비");
        case EItemType::Quest:      return TEXT("퀘스트");
        default:                    return TEXT("일반");
        }
    }
}

TSharedRef<SWidget> UItemTooltipWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TooltipFrame"));
        Frame->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
        Frame->SetPadding(FMargin(12.f, 6.f));

        UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TooltipBox"));
        Frame->SetContent(Box);

        NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NameText"));
        DetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailText"));

        // 월드 공간 위젯이라 기본 크기로는 멀리서 안 읽힌다. 폰트는 슬레이트 기본을 그대로 쓰되
        // 크기만 키운다 — 한글은 이 경로에서 폰트 폴백으로 그려진다.
        FSlateFontInfo Font = NameText->GetFont();
        Font.Size = 28;
        NameText->SetFont(Font);
        Font.Size = 18;
        DetailText->SetFont(Font);
        DetailText->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, 1.f)));

        Box->AddChildToVerticalBox(NameText);
        Box->AddChildToVerticalBox(DetailText);

        WidgetTree->RootWidget = Frame;
    }

    return Super::RebuildWidget();
}

void UItemTooltipWidget::SetItem(const FItemData& Data, int32 Amount)
{
    if (NameText)
    {
        // DisplayName 이 비어 있으면 ID 로 대신한다 — 이름 없는 행이 빈 이름표로 뜨는 것보다 낫다.
        NameText->SetText(Data.DisplayName.IsEmpty() ? FText::FromString(Data.ItemID) : Data.DisplayName);
        NameText->SetColorAndOpacity(FSlateColor(ColorForType(Data.ItemType)));
    }

    if (DetailText)
    {
        const FString Detail = (Amount > 1)
            ? FString::Printf(TEXT("%s  x%d"), *LabelForType(Data.ItemType), Amount)
            : LabelForType(Data.ItemType);
        DetailText->SetText(FText::FromString(Detail));
    }
}
