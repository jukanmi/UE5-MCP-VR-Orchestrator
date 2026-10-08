#pragma once

#include "CoreMinimal.h"
#include "UI/Components/WorldUIComponent.h"
#include "MenuPanelUIComponent.generated.h"

/**
 * 메뉴 패널 — 왼손 Menu 버튼으로 열고 닫는다. 월드를 멈추지 않는다(VR 에서 월드·폰을 멈추면 시야가 굳어 멀미가 나고,
 * 서버 LLM 응답·NPC 흐름이 계속되는 구조와도 충돌한다).
 *
 * 열 때 시선 앞 OpenDistance 에 놓고 그 자리에 고정한다(카메라를 향해 회전만). 머리를 따라오지 않는다 —
 * 시야에 고정된 패널은 멀미를 부른다. 열 때마다 위치를 다시 잡는다.
 * 오른손 UI 포인터가 눌러야 하므로 Visibility 채널만 막는다.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UMenuPanelUIComponent : public UWorldUIComponent
{
    GENERATED_BODY()

public:
    UMenuPanelUIComponent();

    /** 여는 순간 시선 앞 거리(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "40.0", ClampMax = "300.0"))
    float OpenDistance = 100.f;

    /** 시선 높이 기준 상하 오프셋(cm). 음수 = 눈높이보다 아래. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    float OpenHeightOffset = -5.f;

    /** 시선 앞에 놓고 보이게 한다. 카메라가 없으면(헤드셋 없는 PIE 초기 등) 현재 위치에서 그냥 켠다. */
    void Open();

    void Close() { HideUI(); }

    bool IsOpen() const { return IsVisible(); }
};
