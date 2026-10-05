#include "UI/BP/MenuWidget.h"

#include "Components/Button.h"

void UMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ResumeButton)   ResumeButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleResumeClicked);
    if (SettingsButton) SettingsButton->OnClicked.AddUniqueDynamic(this, &UMenuWidget::HandleSettingsClicked);
}

void UMenuWidget::HandleResumeClicked()   { OnResume.Broadcast(); }
void UMenuWidget::HandleSettingsClicked() { OnSettings.Broadcast(); }
