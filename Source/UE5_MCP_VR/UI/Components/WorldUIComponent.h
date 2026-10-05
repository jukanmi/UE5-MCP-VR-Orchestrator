#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "WorldUIComponent.generated.h"

/** 월드 UI 가 카메라를 향하는 방식. */
UENUM(BlueprintType)
enum class EWorldUIFacing : uint8
{
    None          UMETA(DisplayName = "고정"),           // 부모(카메라·액터)를 따라 붙은 채 회전하지 않는다
    FaceCamera    UMETA(DisplayName = "카메라 정면"),    // 상하·좌우 모두 — 손에 든 패널·물건 이름표
    FaceCameraYaw UMETA(DisplayName = "카메라 좌우만"),  // 좌우만 — 머리 위 말풍선처럼 글자를 세워 둔다
};

/**
 * 월드 공간 UI 위젯 공통 — 손 패널·채팅 패널·아이템 이름표·NPC 말풍선이 상속한다.
 *
 * VR 에선 화면 공간 위젯(AddToViewport)을 쓰지 않는다: OpenXR 은 양안을 한 장의 스테레오 타깃에 렌더하고
 * Slate 오버레이는 그 위에 한 번만 합성되어 한쪽 눈에만 뜨거나 좌우로 늘어진다. 월드 공간 위젯은 씬과 같이 양안 렌더된다.
 *
 * 공통 규약:
 *  - 양면 + 반투명, 충돌 없음(포인터가 눌러야 하는 패널만 파생 클래스가 켠다).
 *  - 카메라를 향할 땐 +X 를 카메라 쪽으로 돌린다. 양면이라 어느 면이 앞이어도 글자가 바로 읽힌다
 *    (단면이면 -X 면이 읽히는 면이라 +180° 가 필요했다 — 2026-10-03 PIE 화면 캡처로 확인).
 *  - 보일 때만 틱한다 — 위젯 다시 그리기·카메라 정렬·파생 클래스 갱신(UpdateUI) 모두 그 안에서.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UWorldUIComponent : public UWidgetComponent
{
    GENERATED_BODY()

public:
    UWorldUIComponent();

    /** 카메라를 향하는 방식. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    EWorldUIFacing Facing = EWorldUIFacing::None;

    /** 카메라 정렬 위에 얹는 회전. 0 이면 정확히 카메라를 마주본다 — 살짝 눕히고 싶을 때만 Pitch 를 준다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    FRotator FacingOffset = FRotator::ZeroRotator;

    void ShowUI() { SetVisibility(true); }
    void HideUI() { SetVisibility(false); }

    /** 사용자 위젯(없으면 지금 생성). */
    template <typename T>
    T* GetWidgetAs()
    {
        if (!GetUserWidgetObject()) InitWidget();
        return Cast<T>(GetUserWidgetObject());
    }

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
    virtual void BeginPlay() override;
    virtual void OnVisibilityChanged() override;

    /** 보이는 동안 매 틱(카메라 정렬 뒤) — 파생 클래스가 페이드·자동 닫기 등을 한다. */
    virtual void UpdateUI(float DeltaTime) {}

    /** 플레이어 카메라(HMD) 위치·정면. 없으면 false. */
    bool GetViewPoint(FVector& OutLocation, FVector& OutForward) const;

private:
    void FaceViewer();
};
