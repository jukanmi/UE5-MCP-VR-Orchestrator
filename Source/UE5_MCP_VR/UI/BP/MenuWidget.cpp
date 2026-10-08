#include "UI/BP/MenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Styling/CoreStyle.h"

namespace
{
    const FLinearColor PartyMarkerColor(0.25f, 0.9f, 0.4f, 1.f);
    constexpr float PartyMarkerSize = 14.f;

    // 지도 점 — 검정 테두리 둥근 점. 테두리가 없으면 지도 그림에 묻혀 배경처럼 보인다.
    // 채움색은 위젯의 ColorAndOpacity 가 그대로 입히고, 테두리는 늘 검정.
    void StyleMapDot(UImage* Dot)
    {
        if (!Dot) return;
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
        Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
        Brush.OutlineSettings.Color = FSlateColor(FLinearColor::Black);
        Brush.OutlineSettings.Width = 2.f;
        Dot->SetBrush(Brush);
    }

    void SetGroupVisible(UWidget* Group, bool bVisible)
    {
        if (Group) Group->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
}

void UMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ResumeButton)   ResumeButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleResumeClicked);
    if (MapButton)      MapButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleMapClicked);
    if (PartyButton)    PartyButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandlePartyClicked);
    if (QuestButton)    QuestButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleQuestClicked);
    if (SettingsButton) SettingsButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleSettingsClicked);
    for (UButton* Back : { BackButton, MapBackButton, PartyBackButton, QuestBackButton })
    {
        if (Back) Back->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleBackClicked);
    }
    if (VolumeSlider)   VolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UMenuWidget::HandleVolumeSliderChanged);

    for (UImage* Dot : { PlayerMarker, PlayerHeadingMarker, QuestMarker }) StyleMapDot(Dot);

    if (CalibrationButton)
    {
        CalibrationButton->SetIsEnabled(true);
        CalibrationButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleCalibrationClicked);
    }
    EnsureGuideWidget();

    ShowMain();
}

void UMenuWidget::ShowScreen(EMenuScreen Screen)
{
    CurrentScreen = Screen;
    SetGroupVisible(MainGroup,     Screen == EMenuScreen::Main);
    SetGroupVisible(MapGroup,      Screen == EMenuScreen::Map);
    SetGroupVisible(PartyGroup,    Screen == EMenuScreen::Party);
    SetGroupVisible(QuestGroup,    Screen == EMenuScreen::Quest);
    SetGroupVisible(SettingsGroup, Screen == EMenuScreen::Settings);
}

void UMenuWidget::SetVolumeValue(float Volume)
{
    const float Clamped = FMath::Clamp(Volume, 0.f, 1.f);
    if (VolumeSlider) VolumeSlider->SetValue(Clamped);   // SetValue 는 OnValueChanged 를 쏘지 않는다
    RefreshVolumeText(Clamped);
}

void UMenuWidget::RefreshVolumeText(float Volume)
{
    if (VolumeValueText) VolumeValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Volume * 100.f))));
}

void UMenuWidget::EnsureGuideWidget()
{
    if (bGuideBuilt) return;
    bGuideBuilt = true;
    if (CalibrationGuideText)
    {
        CalibrationGuideText->SetVisibility(ESlateVisibility::Collapsed);   // WBP 가 둔 것 — 비어 있는 동안 숨긴다
        return;
    }
    if (!CalibrationButton || !WidgetTree) return;

    // 측정 버튼을 품은 가장 가까운 세로 상자를 찾아 버튼(또는 버튼을 감싼 위젯) 바로 뒤에 끼운다.
    UWidget* Anchor = CalibrationButton;
    while (Anchor->GetParent() && !Cast<UVerticalBox>(Anchor->GetParent())) Anchor = Anchor->GetParent();
    UVerticalBox* Column = Cast<UVerticalBox>(Anchor->GetParent());
    if (!Column)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MenuWidget] 측정 버튼이 세로 상자 안에 없어 안내 글을 못 붙인다 — WBP 에 CalibrationGuideText 를 두라"));
        return;
    }

    // 어두운 반투명 둥근 상자 + 흰 글자, 가운데 정렬·자동 줄바꿈. 평소엔 Collapsed.
    UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    FSlateBrush BoxBrush;
    BoxBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
    BoxBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    BoxBrush.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
    Box->SetBrush(BoxBrush);
    Box->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.65f));
    Box->SetPadding(FMargin(12.f, 8.f));

    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 26));
    Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Text->SetJustification(ETextJustify::Center);
    Text->SetAutoWrapText(true);
    Box->SetContent(Text);
    Box->SetVisibility(ESlateVisibility::Collapsed);

    if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Column->InsertChildAt(Column->GetChildIndex(Anchor) + 1, Box)))
    {
        BoxSlot->SetHorizontalAlignment(HAlign_Fill);
        BoxSlot->SetPadding(FMargin(0.f, 10.f));
    }
    CalibrationGuideText = Text;
    GuideBox = Box;
}

void UMenuWidget::SetCalibrationLabel(const FText& Label)
{
    if (CalibrationButtonLabel) CalibrationButtonLabel->SetText(Label);
}

void UMenuWidget::SetCalibrationGuide(const FText& Guide)
{
    EnsureGuideWidget();
    if (!CalibrationGuideText) return;
    CalibrationGuideText->SetText(Guide);
    UWidget* Root = GuideBox ? GuideBox.Get() : static_cast<UWidget*>(CalibrationGuideText);
    Root->SetVisibility(Guide.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UMenuWidget::SetQuestInfo(const FString& MainText, const FString& SideText)
{
    if (QuestMainText) QuestMainText->SetText(FText::FromString(MainText));
    if (QuestSideText) QuestSideText->SetText(FText::FromString(SideText));
}

void UMenuWidget::SetPartyEntries(const TArray<FMenuPartyEntry>& Entries)
{
    if (PartyEmptyText) PartyEmptyText->SetVisibility(Entries.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (!PartyList || !WidgetTree) return;

    // 줄 수가 같으면 글만 바꾼다(매 갱신마다 위젯을 새로 만들지 않는다). 다르면 다시 만든다.
    if (PartyList->GetChildrenCount() != Entries.Num())
    {
        PartyList->ClearChildren();
        for (int32 i = 0; i < Entries.Num(); ++i)
        {
            UTextBlock* Row = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            Row->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 28));
            if (UVerticalBoxSlot* RowSlot = PartyList->AddChildToVerticalBox(Row)) RowSlot->SetPadding(FMargin(0.f, 6.f));
        }
    }
    for (int32 i = 0; i < Entries.Num(); ++i)
    {
        if (UTextBlock* Row = Cast<UTextBlock>(PartyList->GetChildAt(i)))
        {
            Row->SetText(FText::FromString(FString::Printf(TEXT("%s   HP %d%%"), *Entries[i].Name, FMath::RoundToInt(Entries[i].HealthPct * 100.f))));
        }
    }
}

void UMenuWidget::PlaceMarker(UImage* Marker, FVector2D UV)
{
    if (!Marker) return;
    if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
    {
        const FVector2D Clamped(FMath::Clamp(UV.X, 0.f, 1.f), FMath::Clamp(UV.Y, 0.f, 1.f));
        MarkerSlot->SetAnchors(FAnchors(Clamped.X, Clamped.Y));
        MarkerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        MarkerSlot->SetPosition(FVector2D::ZeroVector);
    }
}

void UMenuWidget::SetMapMarkers(bool bPlayer, FVector2D PlayerUV, FVector2D PlayerHeadingUV,
                                bool bQuest, FVector2D QuestUV, const TArray<FVector2D>& PartyUVs)
{
    if (PlayerMarker)
    {
        PlayerMarker->SetVisibility(bPlayer ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (bPlayer) PlaceMarker(PlayerMarker, PlayerUV);
    }
    if (PlayerHeadingMarker)
    {
        PlayerHeadingMarker->SetVisibility(bPlayer ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (bPlayer) PlaceMarker(PlayerHeadingMarker, PlayerHeadingUV);
    }
    if (QuestMarker)
    {
        QuestMarker->SetVisibility(bQuest ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (bQuest) PlaceMarker(QuestMarker, QuestUV);
    }

    // 일행 점 — 필요한 수만큼 만들어 두고 남는 건 숨긴다.
    if (MapCanvas && WidgetTree)
    {
        while (PartyMarkers.Num() < PartyUVs.Num())
        {
            UImage* Dot = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
            StyleMapDot(Dot);
            Dot->SetColorAndOpacity(PartyMarkerColor);
            if (UCanvasPanelSlot* DotSlot = MapCanvas->AddChildToCanvas(Dot))
            {
                DotSlot->SetSize(FVector2D(PartyMarkerSize, PartyMarkerSize));
                DotSlot->SetAutoSize(false);
            }
            PartyMarkers.Add(Dot);
        }
    }
    for (int32 i = 0; i < PartyMarkers.Num(); ++i)
    {
        const bool bUsed = PartyUVs.IsValidIndex(i);
        PartyMarkers[i]->SetVisibility(bUsed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (bUsed) PlaceMarker(PartyMarkers[i], PartyUVs[i]);
    }
}

void UMenuWidget::HandleResumeClicked()   { OnResume.Broadcast(); }
void UMenuWidget::HandleMapClicked()      { ShowScreen(EMenuScreen::Map); }
void UMenuWidget::HandlePartyClicked()    { ShowScreen(EMenuScreen::Party); }
void UMenuWidget::HandleQuestClicked()    { ShowScreen(EMenuScreen::Quest); }
void UMenuWidget::HandleSettingsClicked() { ShowScreen(EMenuScreen::Settings); }
void UMenuWidget::HandleBackClicked()     { ShowMain(); }
void UMenuWidget::HandleCalibrationClicked() { OnCalibrationRequested.Broadcast(); }

void UMenuWidget::HandleVolumeSliderChanged(float Value)
{
    RefreshVolumeText(Value);
    OnVolumeChanged.Broadcast(Value);
}
