#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/BP/ItemDataAsset.h"   // EEquipmentSlot
#include "VRPlayerUIComponent.generated.h"

class AVRPawn;
class ADroppedItemBase;
class UPlayerHUDWidget;
class UChatWidget;
class UMaterialInstanceDynamic;

/**
 * VR 플레이어 UI — 왼손 패널(HP·스태미나·인벤토리), 채팅 패널, 오른손 UI 포인터, 손 근처 아이템 이름표.
 * 위젯을 띄우는 장면 컴포넌트(HUDWidgetComp·ChatWidgetComp·HUDInteractor·PointerBeam·PointerDot·ItemTooltipComp)는
 * 컨트롤러·카메라에 붙어야 해서 폰이 갖고, 이 컴포넌트는 그것들을 움직이고 켜고 끄는 로직과 상태를 맡는다.
 * 매 틱 폰이 Update 를 부른다(컴포넌트 자체 틱 없음).
 *
 * 인벤토리 열림 중에는 입력 뜻이 바뀐다 — 트리거 = UI 클릭, 오른손 스틱 = 슬롯 이동, 그립·핀치 = 고른 슬롯 발동.
 */
UCLASS(ClassGroup = (VR))
class UE5_MCP_VR_API UVRPlayerUIComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVRPlayerUIComponent();

    /** HUD 위젯 클래스 — BP_VRPawn 에서 WBP 지정. 미지정 시 HUD 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
    TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

    /** 채팅 위젯 클래스 — BP_VRPawn 에서 WBP_Chat 지정. 미지정 시 채팅 UI 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Chat")
    TSubclassOf<UChatWidget> ChatWidgetClass;

    /** 생성된 HUD 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "UI")
    UPlayerHUDWidget* HUDWidget = nullptr;

    /** 생성된 채팅 위젯 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "UI|Chat")
    UChatWidget* ChatWidget = nullptr;

    /** 패널 회전 추가 오프셋. 매 Tick 계산되는 HMD 정면 회전 위에 얹힌다.
     *  0 이면 정확히 카메라를 마주본다 — 살짝 눕히고 싶을 때만 Pitch 를 준다.
     *  컨트롤러 회전을 그대로 쓰지 않는 이유: Grip 포즈 축이 손등 방향과 30~40° 어긋나 있어
     *  고정 오프셋으로는 손목 각도가 바뀔 때마다 패널이 틀어진다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    FRotator HUDPanelRotation = FRotator::ZeroRotator;

    /** 패널이 보이기 시작하는 시선 일치도. HMD 정면 벡터와 패널 방향의 내적 임계.
     *  0.9 ≈ 시야 중심에서 26° 안. 낮출수록 곁눈질에도 켜진다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0.0", ClampMax = "0.99"))
    float HUDGazeDotThreshold = 0.9f;

    /** 패널 페이드 보간 속도. 임계 경계에서 손이 미세하게 떨리면 켜짐/꺼짐이 반복되므로
     *  즉시 토글하지 않고 보간으로 완충한다. 클수록 빠르게 나타난다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "1.0", ClampMax = "30.0"))
    float HUDGazeFadeSpeed = 8.f;

    /** 슬롯 그리드 열 수. 스틱 상하 이동이 몇 칸 건너뛸지 결정한다(WBP SlotGrid 열 수와 맞출 것). */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "1"))
    int32 InventoryGridColumns = 5;

    /** 선택·꺼내기 결과를 화면에 띄운다. 슬롯 강조 UI 가 없는 동안의 임시 피드백. */
    UPROPERTY(EditAnywhere, Category = "UI")
    bool bDebugInventorySelection = true;

    /** 이름표가 뜨는 손-아이템 거리(cm). 쥐기 반경보다 넓어야 "잡을 수 있다"를 미리 알려준다. */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "10.0", ClampMax = "300.0"))
    float TooltipRange = 70.f;

    /** 아이템 위로 이름표를 띄우는 높이(cm). */
    UPROPERTY(EditAnywhere, Category = "UI")
    float TooltipHeightOffset = 15.f;

    /** 광선 굵기(cm 지름). 얇을수록 조준점을 가리지 않지만 멀리서 안 보인다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0.05", ClampMax = "5.0"))
    float PointerBeamThickness = 0.4f;

    /** 히트점 구 지름(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0.2", ClampMax = "10.0"))
    float PointerDotSize = 1.2f;

    /** 광선·히트점 색. 알파는 무시된다(불투명 이미시브). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    FLinearColor PointerColor = FLinearColor(0.15f, 0.75f, 1.f, 1.f);

    /** 위젯을 실제로 겨눴을 때 색 — 슬롯 위에 올라갔는지 색으로 구분된다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    FLinearColor PointerHitColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

    // ── 폰이 부르는 것 ──

    /** BeginPlay — 포인터 비주얼, HUD·채팅 위젯 생성(로컬 플레이어일 때만), 인벤토리 닫힌 상태로 시작. */
    void Init();

    /** 매 틱 — 패널 정면·시선 페이드, 채팅 패널 자동 닫기, 포인터 광선, 아이템 이름표. */
    void Update(float DeltaTime);

    /** 인벤토리 패널 열림 상태 — HUD 위젯과 동기. 열림 중엔 트리거·스틱·그립의 뜻이 바뀐다. */
    bool IsInventoryOpen() const { return bInventoryOpen; }

    /** 인벤토리 열기/닫기(왼손 Y버튼·콘솔). */
    void ToggleInventory();

    /** 인벤토리 상태만 닫힘으로(사망 등) — 포인터를 놓고 끈다. */
    void CloseInventory();

    /** 트리거 누름 — 인벤토리 열림 중이면 UI 클릭으로 쓰고 true(투사체를 쏘지 않는다). */
    bool PressPointer();

    /** 트리거 뗌 — 눌렀던 UI 포인터를 놓는다. */
    void ReleasePointer();

    /** 인벤토리 열림 중 오른손 스틱 = 슬롯 이동. 한 번 기울일 때 한 칸만 가고,
     *  중립으로 돌아와야 다시 먹는다(계속 기울이면 목록이 순식간에 흘러가 버린다). */
    void NavigateInventory(const FVector2D& Stick);

    /** 인벤토리 열림 중 그립·핀치 = 고른 슬롯을 그 손으로 발동(소비=사용 / 장비=장착 / 일반=손에 쥐기). */
    void ActivateSelectedSlot(EEquipmentSlot HandSlot);

    /** Enter 키 — 채팅 패널을 열고 입력 칸에 포커스. */
    void OpenChat();

    /** 콘솔 진단 — 인벤토리 실제 내용 + HUD 위젯 연결 상태 덤프. */
    void DumpInventoryHUD() const;

private:
    AVRPawn* GetPawn() const;

    /** 패널·포인터 표시 동기 — 열림일 때만 포인터와 광선을 켠다.
     *  닫힘 상태에서 포인터를 켜두면 손을 흔들 때 슬롯이 호버되어 오작동한다. */
    void ApplyInventoryPresentation(bool bOpen);

    void UpdateHUDPanelFacing();
    void UpdateHUDPanelGaze(float DeltaTime);
    void UpdateChatPanelVisibility();
    void UpdatePointerVisual();
    void UpdateItemTooltip();

    bool bInventoryOpen = false;

    /** 트리거로 UI 를 누른 상태인지 — 열림 중에만 true. 닫을 때 강제 릴리즈에 쓴다. */
    bool bPointerPressed = false;

    /** 직전 프레임 히트 여부 — 색이 바뀔 때만 파라미터를 쓴다. */
    bool bPointerWasHitting = false;

    /** 현재 패널 불투명도(0~1). 시작은 투명 — 쳐다보기 전엔 안 보인다. */
    float HUDPanelOpacity = 0.f;

    /** 슬롯 이동 재장전 플래그 — 스틱이 중립으로 돌아왔는지. */
    bool bSlotNavArmed = true;

    /** 광선·히트점 공용 머티리얼 인스턴스. 색을 런타임에 바꾸려면 인스턴스가 필요하다. */
    UPROPERTY(Transient)
    UMaterialInstanceDynamic* PointerMID = nullptr;

    /** 직전에 이름표를 그린 아이템 — 대상이 바뀔 때만 텍스트를 다시 만든다(매 틱 SetText 는 비싸다). */
    UPROPERTY(Transient)
    TObjectPtr<ADroppedItemBase> TooltipTarget;
};
