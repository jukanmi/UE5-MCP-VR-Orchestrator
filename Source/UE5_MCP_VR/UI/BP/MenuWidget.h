#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuWidget.generated.h"

class UButton;
class USlider;
class UTextBlock;
class UWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuAction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuVolumeChanged, float, Volume);

/**
 * 메뉴 위젯 기반 — WBP_MenuPanel 이 이 클래스를 부모로 하고 아래 이름의 위젯을 둔다.
 *  - 메인 화면(`MainGroup`): `ResumeButton`(필수) · `SettingsButton`
 *  - 설정 화면(`SettingsGroup`): `VolumeSlider` · `VolumeValueText` · `CalibrationButton`(자리, 비활성) · `BackButton`
 * 위젯은 상태를 소유하지 않는다: 눌림·값 변경은 델리게이트로만 알리고, 열고 닫기·저장은 UVRPlayerUIComponent 가 한다.
 * 화면 전환(메인 ↔ 설정)만 위젯이 직접 한다 — 화면 안의 표시 상태라 게임 상태가 아니다.
 */
UCLASS(Abstract)
class UE5_MCP_VR_API UMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 재개 버튼 눌림 — 메뉴를 닫는다. */
    UPROPERTY(BlueprintAssignable, Category = "Menu")
    FOnMenuAction OnResume;

    /** 볼륨 슬라이더를 사용자가 움직였을 때(선형 0~1). SetVolumeValue 로 넣은 값은 알리지 않는다. */
    UPROPERTY(BlueprintAssignable, Category = "Menu")
    FOnMenuVolumeChanged OnVolumeChanged;

    /** 메인 화면(재개·설정)을 보인다. 메뉴를 열 때마다 여기서 시작한다. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void ShowMain();

    /** 설정 화면을 보인다. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void ShowSettings();

    /** 슬라이더·표시를 갱신한다(이벤트 없음) — 저장값을 불러온 뒤 호출. */
    void SetVolumeValue(float Volume);

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidget))
    UButton* ResumeButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* SettingsButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* BackButton = nullptr;

    /** 신체 측정 진입 자리 — 측정 기능이 생길 때까지 비활성(SPEC_body_measure_prone M2). */
    UPROPERTY(meta = (BindWidgetOptional))
    UButton* CalibrationButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    USlider* VolumeSlider = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* VolumeValueText = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* MainGroup = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* SettingsGroup = nullptr;

private:
    UFUNCTION()
    void HandleResumeClicked();

    UFUNCTION()
    void HandleSettingsClicked();

    UFUNCTION()
    void HandleBackClicked();

    UFUNCTION()
    void HandleVolumeSliderChanged(float Value);

    void RefreshVolumeText(float Volume);
};
