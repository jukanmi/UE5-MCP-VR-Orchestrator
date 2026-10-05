#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuWidget.generated.h"

class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuAction);

/**
 * 메뉴 위젯 기반 — WBP_Menu 가 이 클래스를 부모로 하고 `ResumeButton`·`SettingsButton` 이름의 버튼을 둔다.
 * 위젯은 상태를 소유하지 않는다: 눌림은 델리게이트로만 알리고, 열고 닫기는 UVRPlayerUIComponent 가 한다.
 */
UCLASS(Abstract)
class UE5_MCP_VR_API UMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 재개 버튼 눌림 — 메뉴를 닫는다. */
    UPROPERTY(BlueprintAssignable, Category = "Menu")
    FOnMenuAction OnResume;

    /** 설정 버튼 눌림 — 설정 화면 전환(설정 단계에서 연결). */
    UPROPERTY(BlueprintAssignable, Category = "Menu")
    FOnMenuAction OnSettings;

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidget))
    UButton* ResumeButton = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* SettingsButton = nullptr;

private:
    UFUNCTION()
    void HandleResumeClicked();

    UFUNCTION()
    void HandleSettingsClicked();
};
