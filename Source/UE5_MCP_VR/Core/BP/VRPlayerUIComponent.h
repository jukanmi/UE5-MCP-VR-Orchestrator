#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/BP/ItemDataAsset.h"   // EEquipmentSlot
#include "VRPlayerUIComponent.generated.h"

class AVRPawn;
class ADroppedItemBase;
class UPlayerHUDWidget;
class UChatWidget;
class UMenuWidget;
class USettingsSaveGame;
class UHUDPanelUIComponent;
class UChatPanelUIComponent;
class UMenuPanelUIComponent;
class UItemTooltipUIComponent;
class UWidgetInteractionComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * VR 플레이어 UI 조율 — 왼손 패널·채팅 패널·아이템 이름표(각자 UWorldUIComponent 파생: 카메라 정렬·페이드·자동 닫기)를
 * 띄우는 위젯 클래스와, 그것들 사이의 상태(인벤토리 열림)·오른손 UI 포인터·이름표 대상 고르기를 맡는다.
 * 장면 컴포넌트는 컨트롤러·카메라에 붙어야 해서 폰이 갖는다. 매 틱 폰이 Update 를 부른다(컴포넌트 자체 틱 없음).
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

    // ── 패널 배치·크기 튜닝값 (패널은 Init 에서 만든다 — 폰의 컨트롤러·카메라에 붙인다) ──

    /** HUD 패널의 왼손 컨트롤러 기준 위치(cm). 손등 위쪽에 얹히는 값이 기본. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HUD")
    FVector HUDPanelLocation = FVector(4.f, 0.f, 12.f);

    /** HUD 위젯 가상 캔버스 해상도(px). 실제 월드 크기는 이 값 × HUDPanelScale(1px=1cm 기준).
     *  세로는 인벤토리 패널(350px)과 상태 패널(게이지+채팅 로그+입력창, 292px)이 함께 들어갈
     *  만큼 필요하다 — 모자라면 인벤토리를 연 순간 아래쪽이 캔버스 밖으로 잘려 나간다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HUD")
    FVector2D HUDPanelDrawSize = FVector2D(600.f, 660.f);

    /** HUD 패널 월드 스케일. 기본값은 600x660px → 약 24x26cm (손에 들린 태블릿 크기). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HUD", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float HUDPanelScale = 0.04f;

    /** UI 포인터 광선 길이(cm). 손 패널까지만 닿으면 되므로 짧게. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HUD", meta = (ClampMin = "20.0", ClampMax = "500.0"))
    float HUDInteractionDistance = 150.f;

    /** 채팅 패널의 카메라 기준 로컬 오프셋(cm) — 정면 아래쪽에 배치. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Chat")
    FVector ChatPanelOffset = FVector(80.f, 0.f, -15.f);

    /** 채팅 패널 가상 캔버스 해상도(px). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Chat")
    FVector2D ChatPanelDrawSize = FVector2D(500.f, 260.f);

    /** 채팅 패널 월드 스케일. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Chat", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float ChatPanelScale = 0.08f;

    /** 메뉴 패널 가상 캔버스 해상도(px). 지도 화면(정사각 지도)이 들어가는 크기. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Menu")
    FVector2D MenuPanelDrawSize = FVector2D(640.f, 560.f);

    // ── 메뉴 정보 화면(지도·파티·퀘스트) 데이터 ──

    /** 지도 이미지가 덮는 월드 영역의 중심(cm, X·Y). 지도 촬영 때 쓴 값과 같아야 마커가 맞는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Menu|Map")
    FVector2D MapWorldCenter = FVector2D::ZeroVector;

    /** 지도 이미지가 덮는 월드 한 변 길이(cm, 정사각). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Menu|Map", meta = (ClampMin = "1000.0"))
    float MapWorldSize = 51000.f;

    /** 지도에서 플레이어 방향 점을 얼마나 앞에 찍을지(지도 한 변 대비 비율). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Menu|Map", meta = (ClampMin = "0.0", ClampMax = "0.2"))
    float MapHeadingLength = 0.03f;

    /** 파티 화면 목업 일행(AgentID). `UPartySubsystem`(SPEC_party)이 생기면 이 목업을 지우고 그쪽을 읽는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Menu|Party")
    TArray<FString> MockPartyIds = { TEXT("Elara"), TEXT("James"), TEXT("Skadi") };

    /** 메뉴 패널 월드 스케일(1px = 스케일 cm). 0.12 면 500px 가 약 60cm(세로 380px 는 설정 화면이 들어가는 높이). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Menu", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float MenuPanelScale = 0.12f;

    /** 메뉴 위젯 클래스 — BP_VRPawn 에서 WBP_Menu 지정. 미지정 시 메뉴 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Menu")
    TSubclassOf<UMenuWidget> MenuWidgetClass;

    /** 생성된 HUD 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "UI")
    UPlayerHUDWidget* HUDWidget = nullptr;

    /** 생성된 채팅 위젯 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "UI|Chat")
    UChatWidget* ChatWidget = nullptr;

    /** 생성된 메뉴 위젯 인스턴스 (런타임). */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "UI|Menu")
    UMenuWidget* MenuWidget = nullptr;

    /** 슬롯 그리드 열 수. 스틱 상하 이동이 몇 칸 건너뛸지 결정한다(WBP SlotGrid 열 수와 맞출 것). */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "1"))
    int32 InventoryGridColumns = 5;

    /** 선택·꺼내기 결과를 화면에 띄운다. 슬롯 강조 UI 가 없는 동안의 임시 피드백. */
    UPROPERTY(EditAnywhere, Category = "UI")
    bool bDebugInventorySelection = true;

    /** 이름표가 뜨는 손-아이템 거리(cm). 쥐기 반경보다 넓어야 "잡을 수 있다"를 미리 알려준다. */
    UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "10.0", ClampMax = "300.0"))
    float TooltipRange = 70.f;

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

    /** 매 틱 — 포인터 광선, 아이템 이름표 대상. */
    void Update(float DeltaTime);

    /** 인벤토리 패널 열림 상태 — HUD 위젯과 동기. 열림 중엔 트리거·스틱·그립의 뜻이 바뀐다. */
    bool IsInventoryOpen() const { return bInventoryOpen; }

    /** UI 가 게임 입력(이동·회전·공격·잡기·대시·상호작용)을 막고 있는지 — 메뉴가 열려 있는 동안 true. 폰이 입력마다 묻는다. */
    bool BlocksGameplayInput() const { return bMenuOpen; }

    /** 메뉴 열림 상태. 열림 중엔 이동·회전·공격·잡기·대시·상호작용이 막히고 트리거는 UI 클릭이 된다. */
    bool IsMenuOpen() const { return bMenuOpen; }

    /** 메뉴 열기/닫기(왼손 Menu 버튼). 열 때 인벤토리는 닫는다. */
    void ToggleMenu();

    /** 메뉴 닫기(재개 버튼·사망 등). */
    void CloseMenu();

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

    /** 패널·포인터·이름표 장면 컴포넌트를 만들어 폰의 컨트롤러·카메라에 붙인다(한 번만). */
    void CreateSceneComponents();

    /** 패널·포인터 표시 동기 — 열림일 때만 포인터와 광선을 켠다.
     *  닫힘 상태에서 포인터를 켜두면 손을 흔들 때 슬롯이 호버되어 오작동한다. */
    void ApplyInventoryPresentation(bool bOpen);

    /** 포인터(광선·히트점·위젯 상호작용)를 인벤토리 또는 메뉴가 열려 있을 때만 켠다. */
    void SyncPointer();

    UFUNCTION()
    void OnMenuResume();

    UFUNCTION()
    void OnMenuVolumeChanged(float Volume);

    /** 설정 화면의 측정 버튼 — 측정 중이면 취소, 아니면 시작. 상태는 UVRBodyMeasureComponent 가 갖는다. */
    UFUNCTION()
    void OnMenuCalibration();

    /** 측정 상태기계의 안내·버튼 글을 메뉴 위젯에 밀어 넣는다. */
    UFUNCTION()
    void OnBodyMeasureText(const FText& Guide, const FText& ButtonLabel);

    /** 메뉴가 열려 있는 동안 현재 화면에 필요한 데이터를 위젯에 밀어 넣는다(Update 가 부른다). */
    void UpdateMenuInfo();

    /** 월드 좌표 → 지도 UV(0~1, 왼쪽 위 0,0). 위에서 내려다본 이미지에서 +X 는 위, +Y 는 오른쪽. */
    FVector2D WorldToMapUV(const FVector& World) const;

    /** 저장된 설정을 불러와(없으면 기본값) 메뉴에 반영하고 적용한다. */
    void LoadSettings();

    /** 바뀐 설정이 있으면 슬롯에 쓴다. 메뉴를 닫을 때 부른다(슬라이더를 끄는 동안 매 프레임 쓰지 않는다). */
    void SaveSettings();

    /** 마스터 서브믹스 출력 볼륨(선형) — 모든 소리에 일괄 적용. */
    void ApplyMasterVolume(float Volume);

    void UpdatePointerVisual();
    void UpdateItemTooltip();

    bool bInventoryOpen = false;
    bool bMenuOpen = false;

    UPROPERTY(Transient)
    USettingsSaveGame* Settings = nullptr;

    /** 마지막 저장 이후 설정이 바뀌었는지. */
    bool bSettingsDirty = false;

    /** 정보 화면 갱신 간격(초) 누적 — 지도는 매 틱, 파티·퀘스트 글은 이 간격으로. */
    float MenuInfoAccum = 0.f;

    /** 트리거로 UI 를 누른 상태인지 — 열림 중에만 true. 닫을 때 강제 릴리즈에 쓴다. */
    bool bPointerPressed = false;

    /** 직전 프레임 히트 여부 — 색이 바뀔 때만 파라미터를 쓴다. */
    bool bPointerWasHitting = false;

    /** 슬롯 이동 재장전 플래그 — 스틱이 중립으로 돌아왔는지. */
    bool bSlotNavArmed = true;

    /** 광선·히트점 공용 머티리얼 인스턴스. 색을 런타임에 바꾸려면 인스턴스가 필요하다. */
    UPROPERTY(Transient)
    UMaterialInstanceDynamic* PointerMID = nullptr;

    // ── 런타임에 만든 장면 컴포넌트 (GC 보호용 UPROPERTY) ──

    /** 왼손 컨트롤러에 얹힌 HUD 패널. 카메라 부착(head-lock)은 피한다 — 상시 표시 패널이 시야에 고정되면 멀미. */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "UI")
    UHUDPanelUIComponent* HUDPanel = nullptr;

    /** 카메라에 붙은 채팅 패널 — Enter 로 열 때만 잠깐 시야에 고정된다. */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "UI")
    UChatPanelUIComponent* ChatPanel = nullptr;

    /** 열 때 시선 앞에 놓이고 그 자리에 고정되는 메뉴 패널. */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "UI")
    UMenuPanelUIComponent* MenuPanel = nullptr;

    /** 아이템 이름표 하나 — 대상만 바꿔 손 근처 아이템 위로 옮겨 쓴다. */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "UI")
    UItemTooltipUIComponent* ItemTooltip = nullptr;

    /** 오른손 UI 포인터 — 월드 공간 위젯엔 마우스가 없으므로 광선을 쏴 가상 포인터 이벤트로 바꾼다. */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "UI")
    UWidgetInteractionComponent* Interactor = nullptr;

    /** 포인터 광선 실메시 — bShowDebug 는 Shipping 에서 컴파일 제외라 출시본에도 남는 메시로 그린다. */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "UI")
    UStaticMeshComponent* PointerBeam = nullptr;

    /** 광선이 맞은 지점의 작은 구. 맞은 게 없으면 숨는다. */
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "UI")
    UStaticMeshComponent* PointerDot = nullptr;

};
