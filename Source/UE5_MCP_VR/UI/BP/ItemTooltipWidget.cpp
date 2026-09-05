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
    // WidgetTree 는 위젯 블루프린트가 초기화할 때만 채워진다. WBP 없이 이 C++ 클래스를
    // 그대로 위젯 클래스로 쓰면 null 이라, 없으면 여기서 직접 만든다.
    // (2026-09-06 실측: 이 가드가 없어 트리가 안 지어지고 빈 위젯만 떠 있었다.)
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    }

    if (!WidgetTree->RootWidget)
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
