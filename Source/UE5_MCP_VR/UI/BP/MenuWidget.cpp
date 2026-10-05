#include "UI/BP/MenuWidget.h"

#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"

void UMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ResumeButton)   ResumeButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleResumeClicked);
    if (SettingsButton) SettingsButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleSettingsClicked);
    if (BackButton)     BackButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleBackClicked);
    if (VolumeSlider)   VolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UMenuWidget::HandleVolumeSliderChanged);

    // 신체 측정은 아직 없다 — 자리만 보이고 눌리지 않는다.
    if (CalibrationButton) CalibrationButton->SetIsEnabled(false);

    ShowMain();
}

void UMenuWidget::ShowMain()
{
    if (MainGroup)     MainGroup->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if (SettingsGroup) SettingsGroup->SetVisibility(ESlateVisibility::Collapsed);
}

void UMenuWidget::ShowSettings()
{
    if (MainGroup)     MainGroup->SetVisibility(ESlateVisibility::Collapsed);
    if (SettingsGroup) SettingsGroup->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
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

void UMenuWidget::HandleResumeClicked()   { OnResume.Broadcast(); }
void UMenuWidget::HandleSettingsClicked() { ShowSettings(); }
void UMenuWidget::HandleBackClicked()     { ShowMain(); }

void UMenuWidget::HandleVolumeSliderChanged(float Value)
{
    RefreshVolumeText(Value);
    OnVolumeChanged.Broadcast(Value);
}
