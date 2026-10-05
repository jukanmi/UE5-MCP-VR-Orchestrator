#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class USlider;
class UTextBlock;
class UVerticalBox;
class UWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuAction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuVolumeChanged, float, Volume);

/** 메뉴 화면 종류 — 메인에서 각 화면으로 들어가고 각 화면의 뒤로 버튼이 메인으로 돌아온다. */
UENUM(BlueprintType)
enum class EMenuScreen : uint8
{
    Main,
    Map,
    Party,
    Quest,
    Settings,
};

/** 파티 화면 한 줄 — 화면은 이 목록만 읽는다(소스는 UVRPlayerUIComponent 가 정한다). */
USTRUCT(BlueprintType)
struct FMenuPartyEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Menu")
    FString Name;

    /** 0~1. */
    UPROPERTY(BlueprintReadWrite, Category = "Menu")
    float HealthPct = 1.f;
};

/**
 * 메뉴 위젯 기반 — WBP_MenuPanel 이 이 클래스를 부모로 하고 아래 이름의 위젯을 둔다.
 *  - 메인(`MainGroup`): `ResumeButton`(필수) · `MapButton` · `PartyButton` · `QuestButton` · `SettingsButton`
 *  - 지도(`MapGroup`): `MapCanvas`(고정 크기 패널) 안의 `MapImage`·`PlayerMarker`·`PlayerHeadingMarker`·`QuestMarker`, `MapBackButton`
 *  - 파티(`PartyGroup`): `PartyList`(세로 상자, 줄은 코드가 만든다)·`PartyEmptyText`, `PartyBackButton`
 *  - 퀘스트(`QuestGroup`): `QuestMainText`·`QuestSideText`, `QuestBackButton`
 *  - 설정(`SettingsGroup`): `VolumeSlider`·`VolumeValueText`·`CalibrationButton`(자리, 비활성)·`BackButton`
 * 위젯은 상태를 소유하지 않는다: 눌림·값 변경은 델리게이트로만 알리고, 열고 닫기·저장·데이터 공급은 UVRPlayerUIComponent 가 한다.
 * 화면 전환(메인 ↔ 각 화면)만 위젯이 직접 한다 — 화면 안의 표시 상태라 게임 상태가 아니다.
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

    /** 메인 화면을 보인다. 메뉴를 열 때마다 여기서 시작한다. */
    UFUNCTION(BlueprintCallable, Category = "Menu")
    void ShowMain() { ShowScreen(EMenuScreen::Main); }

    UFUNCTION(BlueprintCallable, Category = "Menu")
    void ShowSettings() { ShowScreen(EMenuScreen::Settings); }

    UFUNCTION(BlueprintCallable, Category = "Menu")
    void ShowScreen(EMenuScreen Screen);

    UFUNCTION(BlueprintPure, Category = "Menu")
    EMenuScreen GetScreen() const { return CurrentScreen; }

    /** 슬라이더·표시를 갱신한다(이벤트 없음) — 저장값을 불러온 뒤 호출. */
    void SetVolumeValue(float Volume);

    /** 퀘스트 화면 글 — 메인 목표 한 문장과 서브퀘스트 줄들(개행으로 구분). */
    void SetQuestInfo(const FString& MainText, const FString& SideText);

    /** 파티 화면 — 목록이 비면 "일행 없음" 을 보인다. */
    void SetPartyEntries(const TArray<FMenuPartyEntry>& Entries);

    /** 지도 마커 위치(UV 0~1, 이미지 왼쪽 위가 0,0). 숨길 마커는 false. 일행 점은 필요한 수만큼 만들어 쓴다. */
    void SetMapMarkers(bool bPlayer, FVector2D PlayerUV, FVector2D PlayerHeadingUV,
                       bool bQuest, FVector2D QuestUV, const TArray<FVector2D>& PartyUVs);

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidget))
    UButton* ResumeButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MapButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* PartyButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* QuestButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* SettingsButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* BackButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MapBackButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* PartyBackButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* QuestBackButton = nullptr;

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
    UWidget* MapGroup = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* PartyGroup = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* QuestGroup = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* SettingsGroup = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* QuestMainText = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* QuestSideText = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UVerticalBox* PartyList = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* PartyEmptyText = nullptr;

    /** 지도 마커를 얹는 고정 크기 패널 — 마커는 앵커(0~1 비율)로 놓아 픽셀 계산이 없다. */
    UPROPERTY(meta = (BindWidgetOptional))
    UCanvasPanel* MapCanvas = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UImage* PlayerMarker = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UImage* PlayerHeadingMarker = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UImage* QuestMarker = nullptr;

private:
    UFUNCTION()
    void HandleResumeClicked();
    UFUNCTION()
    void HandleMapClicked();
    UFUNCTION()
    void HandlePartyClicked();
    UFUNCTION()
    void HandleQuestClicked();
    UFUNCTION()
    void HandleSettingsClicked();
    UFUNCTION()
    void HandleBackClicked();

    UFUNCTION()
    void HandleVolumeSliderChanged(float Value);

    void RefreshVolumeText(float Volume);

    /** 앵커 비율(0~1)로 마커를 놓는다 — 캔버스 슬롯이 아니면 무시. */
    static void PlaceMarker(UImage* Marker, FVector2D UV);

    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> PartyMarkers;

    EMenuScreen CurrentScreen = EMenuScreen::Main;
};
