#pragma once

#include "CoreMinimal.h"
#include "UI/Components/WorldUIComponent.h"
#include "HUDPanelUIComponent.generated.h"

/**
 * 왼손 패널(HP·스태미나·인벤토리) — 손등 위에 얹혀 손을 따라가고 회전만 카메라를 향한다.
 * 상시 표시지만 손목을 쳐다볼 때만 서서히 나타난다(상시 불투명이면 왼손이 시야에 들어올 때마다 앞을 가린다).
 * 인벤토리를 연 동안엔 시선과 무관하게 불투명(슬롯을 조준하다 고개가 조금 돌아갔다고 흐려지면 조작이 끊긴다).
 * 오른손 UI 포인터가 눌러야 하므로 Visibility 채널만 막는다.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UHUDPanelUIComponent : public UWorldUIComponent
{
    GENERATED_BODY()

public:
    UHUDPanelUIComponent();

    /** 패널이 보이기 시작하는 시선 일치도. HMD 정면 벡터와 패널 방향의 내적 임계.
     *  0.9 ≈ 시야 중심에서 26° 안. 낮출수록 곁눈질에도 켜진다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0.0", ClampMax = "0.99"))
    float GazeDotThreshold = 0.9f;

    /** 페이드 보간 속도. 임계 경계에서 손이 미세하게 떨리면 켜짐/꺼짐이 반복되므로
     *  즉시 토글하지 않고 보간으로 완충한다. 클수록 빠르게 나타난다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "1.0", ClampMax = "30.0"))
    float GazeFadeSpeed = 8.f;

    /** true 면 시선과 무관하게 불투명(인벤토리 열림 중). */
    bool bForceOpaque = false;

protected:
    virtual void UpdateUI(float DeltaTime) override;

private:
    /** 현재 불투명도(0~1). 시작은 투명 — 쳐다보기 전엔 안 보인다. */
    float Opacity = 0.f;
};
