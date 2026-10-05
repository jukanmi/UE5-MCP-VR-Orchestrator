#include "Core/BP/VRPlayerUIComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Core/BP/VRPawn.h"
#include "Core/Utils/EngineShapes.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "Core/Save/SettingsSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundSubmix.h"
#include "NPC/BP/SmartNPC.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Story/StorySubsystem.h"
#include "UI/BP/ChatWidget.h"
#include "UI/BP/MenuWidget.h"
#include "UI/Components/MenuPanelUIComponent.h"
#include "UI/Components/ChatPanelUIComponent.h"
#include "UI/Components/HUDPanelUIComponent.h"
#include "UI/Components/ItemTooltipUIComponent.h"
#include "UI/BP/PlayerHUDWidget.h"

UVRPlayerUIComponent::UVRPlayerUIComponent()
{
    PrimaryComponentTick.bCanEverTick = false;   // 폰이 Update 를 부른다
}

AVRPawn* UVRPlayerUIComponent::GetPawn() const
{
    return Cast<AVRPawn>(GetOwner());
}

void UVRPlayerUIComponent::CreateSceneComponents()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn || HUDPanel) return;   // 한 번만

    // 런타임 생성 컴포넌트는 등록 + 인스턴스 컴포넌트 목록에 올려야 에디터 상세·직렬화·GC 에서 폰의 일부로 취급된다.
    auto Register = [Pawn](USceneComponent* Comp)
    {
        Comp->RegisterComponent();
        Pawn->AddInstanceComponent(Comp);
    };

    // HUD 패널 — 왼손 컨트롤러에 얹힌 월드 공간 위젯. 카메라 정렬·시선 페이드·포인터 충돌은 UHUDPanelUIComponent.
    HUDPanel = NewObject<UHUDPanelUIComponent>(Pawn, TEXT("HUDWidgetComp"));
    HUDPanel->SetupAttachment(Pawn->MotionControllerLeft);
    HUDPanel->SetRelativeLocation(HUDPanelLocation);
    HUDPanel->SetDrawSize(HUDPanelDrawSize);
    HUDPanel->SetRelativeScale3D(FVector(HUDPanelScale));
    Register(HUDPanel);

    // 채팅 패널 — 예외적으로 카메라 부착(이유·자동 닫기는 UChatPanelUIComponent).
    // 카메라 앞에 있으니 Yaw 180 으로 카메라 쪽을 본다(양면이라 어느 면이든 읽힌다).
    ChatPanel = NewObject<UChatPanelUIComponent>(Pawn, TEXT("ChatWidgetComp"));
    ChatPanel->SetupAttachment(Pawn->VRCamera);
    ChatPanel->SetRelativeLocation(ChatPanelOffset);
    ChatPanel->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    ChatPanel->SetDrawSize(ChatPanelDrawSize);
    ChatPanel->SetRelativeScale3D(FVector(ChatPanelScale));
    Register(ChatPanel);

    // 메뉴 패널 — 루트에 붙지만 월드 고정(절대 좌표). 열 때 시선 앞에 놓는다(UMenuPanelUIComponent).
    MenuPanel = NewObject<UMenuPanelUIComponent>(Pawn, TEXT("MenuWidgetComp"));
    MenuPanel->SetupAttachment(Pawn->GetRootComponent());
    MenuPanel->SetDrawSize(MenuPanelDrawSize);
    MenuPanel->SetRelativeScale3D(FVector(MenuPanelScale));
    Register(MenuPanel);

    // UI 포인터 — 오른손 Aim 포즈 기준. Grip 포즈는 자연 조준축에서 ~30° 틀어져 있어 광선이 패널을 빗나간다.
    Interactor = NewObject<UWidgetInteractionComponent>(Pawn, TEXT("HUDInteractor"));
    Interactor->SetupAttachment(Pawn->MotionControllerRightAim);
    Interactor->InteractionDistance = HUDInteractionDistance;
    Interactor->InteractionSource = EWidgetInteractionSource::World;
    Interactor->bEnableHitTesting = true;
    Interactor->bShowDebug = false;          // 조준이 안 맞을 때 켜서 광선 확인
    Interactor->bAutoActivate = false;       // 인벤토리·메뉴 열림 중에만 활성
    Register(Interactor);

    // 포인터 광선 실메시 — 원통을 조준축(+X)으로 눕혀 길이만 늘린다.
    // 기본 원통은 Z축 100cm 이므로 Pitch -90 으로 Z 를 부모의 +X 에 맞춘다.
    PointerBeam = NewObject<UStaticMeshComponent>(Pawn, TEXT("PointerBeam"));
    PointerBeam->SetupAttachment(Pawn->MotionControllerRightAim);
    PointerBeam->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
    // 광선이 자기 자신을 맞고 멈추지 않도록 콜리전 완전 차단. 그림자도 끈다(가는 막대의 그림자는 노이즈).
    PointerBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PointerBeam->SetCastShadow(false);
    PointerBeam->SetVisibility(false);
    Register(PointerBeam);

    PointerDot = NewObject<UStaticMeshComponent>(Pawn, TEXT("PointerDot"));
    PointerDot->SetupAttachment(Pawn->MotionControllerRightAim);
    PointerDot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PointerDot->SetCastShadow(false);
    PointerDot->SetVisibility(false);
    Register(PointerDot);

    // 아이템 이름표 — 하나를 들고 다니며 손 근처 아이템 위로 옮긴다(위치는 매 틱 월드 좌표).
    ItemTooltip = NewObject<UItemTooltipUIComponent>(Pawn, TEXT("ItemTooltipComp"));
    ItemTooltip->SetupAttachment(Pawn->GetRootComponent());
    Register(ItemTooltip);
}

void UVRPlayerUIComponent::Init()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn) return;

    CreateSceneComponents();

    // 포인터 비주얼 에셋 — 엔진 기본 도형 + 이미시브 머티리얼. 프로젝트 에셋을 만들지 않으려는 선택으로,
    // 셋 중 하나라도 없으면 포인터만 조용히 안 보이고 클릭 기능 자체는 그대로 동작한다.
    if (PointerBeam && PointerDot)
    {
        if (UStaticMesh* Cylinder = EngineShapes::LoadCylinder()) PointerBeam->SetStaticMesh(Cylinder);
        if (UStaticMesh* Sphere   = EngineShapes::LoadSphere())   PointerDot->SetStaticMesh(Sphere);

        // 빔·점이 한 MID 를 공유 — 조준 적중 시 색을 한 번에 바꾼다.
        PointerMID = EngineShapes::MakeEmissiveMID(this, PointerColor);
        if (PointerMID)
        {
            PointerBeam->SetMaterial(0, PointerMID);
            PointerDot->SetMaterial(0, PointerMID);
        }

        // 굵기·크기는 여기서 한 번만. 길이(Z)는 매 Tick 조준 거리로 덮어쓴다.
        PointerDot->SetRelativeScale3D(FVector(PointerDotSize / 100.f));
    }

    // HUD·채팅 위젯 생성 — 로컬 플레이어 컨트롤러일 때만.
    // 위젯은 뷰포트가 아니라 왼손 패널(HUDWidgetComp)에 실린다. 화면 공간 위젯은
    // 스테레오 렌더 타깃 위에 한 번만 합성되어 한쪽 눈에만 보이기 때문.
    APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    if (PC && PC->IsLocalController())
    {
        if (HUDWidgetClass && HUDPanel)
        {
            HUDPanel->SetOwnerPlayer(PC->GetLocalPlayer());
            HUDPanel->SetWidgetClass(HUDWidgetClass);
            HUDPanel->InitWidget();
            HUDWidget = Cast<UPlayerHUDWidget>(HUDPanel->GetUserWidgetObject());
        }
        if (ChatWidgetClass && ChatPanel)
        {
            ChatPanel->SetOwnerPlayer(PC->GetLocalPlayer());
            ChatPanel->SetWidgetClass(ChatWidgetClass);
            ChatPanel->InitWidget();
            ChatWidget = Cast<UChatWidget>(ChatPanel->GetUserWidgetObject());
        }
    }

    if (PC && PC->IsLocalController() && MenuWidgetClass && MenuPanel)
    {
        MenuPanel->SetOwnerPlayer(PC->GetLocalPlayer());
        MenuPanel->SetWidgetClass(MenuWidgetClass);
        MenuPanel->InitWidget();
        MenuWidget = Cast<UMenuWidget>(MenuPanel->GetUserWidgetObject());
        if (MenuWidget)
        {
            MenuWidget->OnResume.AddUniqueDynamic(this, &UVRPlayerUIComponent::OnMenuResume);
            MenuWidget->OnVolumeChanged.AddUniqueDynamic(this, &UVRPlayerUIComponent::OnMenuVolumeChanged);
        }
    }

    // 메뉴 위젯이 없어도 저장된 볼륨은 적용한다.
    LoadSettings();

    // HUDWidgetComp 가 블루프린트 직렬화 캐시 등으로 비활성화되어 있는 경우 방어
    if (HUDPanel)
    {
        HUDPanel->SetVisibility(true);
    }

    // 인벤토리는 닫힌 상태로 시작 — 포인터를 끄고 슬롯을 접는다.
    // 패널 자체는 계속 켜져 있다(HP·스태미나 게이지가 실려 있음).
    ApplyInventoryPresentation(false);
}

void UVRPlayerUIComponent::Update(float DeltaTime)
{
    UpdatePointerVisual();
    UpdateItemTooltip();

    if (bMenuOpen && MenuWidget)
    {
        MenuInfoAccum += DeltaTime;
        UpdateMenuInfo();
    }
}

// ============================================================================
// 인벤토리 패널
// ============================================================================

void UVRPlayerUIComponent::ToggleInventory()
{
    if (!HUDWidget || bMenuOpen) return;   // 메뉴가 열려 있으면 인벤토리를 열지 않는다
    bInventoryOpen = HUDWidget->ToggleInventoryVisibility();
    ApplyInventoryPresentation(bInventoryOpen);
}

void UVRPlayerUIComponent::CloseInventory()
{
    if (!bInventoryOpen) return;
    bInventoryOpen = false;
    ApplyInventoryPresentation(false);
}

void UVRPlayerUIComponent::ApplyInventoryPresentation(bool bOpen)
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn) return;

    // 패널은 숨기지 않는다 — HP·스태미나 게이지가 상시 표시되고 인벤토리 슬롯만 위젯 내부에서 접힌다.
    // 연 동안엔 시선과 무관하게 불투명(슬롯을 조준하다 고개가 조금 돌아갔다고 흐려지면 조작이 끊긴다).
    if (HUDPanel) HUDPanel->bForceOpaque = bOpen;
    SyncPointer();
}

void UVRPlayerUIComponent::SyncPointer()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn) return;

    const bool bOn = bInventoryOpen || bMenuOpen;
    if (Interactor)
    {
        // 닫을 때 눌린 채로 두면 다음에 열었을 때 첫 클릭이 씹힌다.
        if (!bOn && bPointerPressed)
        {
            Interactor->ReleasePointerKey(EKeys::LeftMouseButton);
            bPointerPressed = false;
        }
        Interactor->SetActive(bOn);
        Interactor->SetVisibility(bOn);
    }

    // 닫는 프레임에 바로 끈다 — Tick 을 기다리면 한 프레임 광선이 남는다.
    if (!bOn)
    {
        if (PointerBeam) PointerBeam->SetVisibility(false);
        if (PointerDot)  PointerDot->SetVisibility(false);
    }
}

// ============================================================================
// 메뉴
// ============================================================================

void UVRPlayerUIComponent::ToggleMenu()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn || !MenuPanel || !MenuWidget) return;   // 위젯 클래스 미지정이면 메뉴 없음

    if (bMenuOpen)
    {
        CloseMenu();
        return;
    }

    CloseInventory();   // 포인터·슬롯 입력 규칙이 겹치지 않게 인벤토리는 먼저 닫는다
    bMenuOpen = true;
    MenuWidget->ShowMain();   // 항상 메인 화면에서 시작
    MenuPanel->Open();
    SyncPointer();
}

void UVRPlayerUIComponent::CloseMenu()
{
    if (!bMenuOpen) return;
    bMenuOpen = false;
    if (MenuPanel) MenuPanel->Close();
    SaveSettings();
    SyncPointer();
}

void UVRPlayerUIComponent::OnMenuResume()
{
    CloseMenu();
}

// ============================================================================
// 정보 화면 — 지도·파티·퀘스트
// ============================================================================

FVector2D UVRPlayerUIComponent::WorldToMapUV(const FVector& World) const
{
    // 위에서 내려다본 지도 이미지: 위쪽 = 월드 +X, 오른쪽 = 월드 +Y. 이미지 왼쪽 위가 (0,0).
    const float Size = FMath::Max(MapWorldSize, 1.f);
    return FVector2D((World.Y - MapWorldCenter.Y) / Size + 0.5f, 0.5f - (World.X - MapWorldCenter.X) / Size);
}

void UVRPlayerUIComponent::UpdateMenuInfo()
{
    AVRPawn* Pawn = GetPawn();
    if (!MenuWidget || !Pawn) return;

    switch (MenuWidget->GetScreen())
    {
    case EMenuScreen::Map:
    {
        // 플레이어 위치·방향(HMD 정면) — 방향 점은 앞쪽으로 조금 떨어진 곳.
        const FVector Loc = Pawn->GetActorLocation();
        FVector Fwd = Pawn->VRCamera ? Pawn->VRCamera->GetForwardVector() : Pawn->GetActorForwardVector();
        Fwd.Z = 0.f;
        Fwd = Fwd.GetSafeNormal();
        const FVector2D PlayerUV = WorldToMapUV(Loc);
        const FVector2D HeadingUV = PlayerUV + FVector2D(Fwd.Y, -Fwd.X) * MapHeadingLength;

        // 퀘스트 목표 — 월드 마커와 같은 대상.
        bool bQuest = false;
        FVector2D QuestUV = FVector2D::ZeroVector;
        if (const UStorySubsystem* Story = UStorySubsystem::Get(this))
        {
            if (const AActor* Target = Story->GetQuestTargetActor())
            {
                bQuest = true;
                QuestUV = WorldToMapUV(Target->GetActorLocation());
            }
        }

        // 일행(목업) 위치.
        TArray<FVector2D> PartyUVs;
        if (UNPCManager* Manager = UNPCManager::Get(this))
        {
            for (const FString& Id : MockPartyIds)
            {
                if (const ASmartNPC* NPC = Manager->GetNPCById(Id)) PartyUVs.Add(WorldToMapUV(NPC->GetActorLocation()));
            }
        }
        MenuWidget->SetMapMarkers(true, PlayerUV, HeadingUV, bQuest, QuestUV, PartyUVs);
        break;
    }
    case EMenuScreen::Party:
    {
        if (MenuInfoAccum < 0.25f) break;
        MenuInfoAccum = 0.f;
        TArray<FMenuPartyEntry> Entries;
        if (UNPCManager* Manager = UNPCManager::Get(this))
        {
            for (const FString& Id : MockPartyIds)
            {
                ASmartNPC* NPC = Manager->GetNPCById(Id);
                if (!NPC) continue;
                FMenuPartyEntry Entry;
                Entry.Name = Id;
                Entry.HealthPct = FMath::Clamp(INPC::Execute_GetNPCAttributes(NPC).Resources.GetHealthPercent(), 0.f, 1.f);
                Entries.Add(Entry);
            }
        }
        MenuWidget->SetPartyEntries(Entries);
        break;
    }
    case EMenuScreen::Quest:
    {
        if (MenuInfoAccum < 0.25f) break;
        MenuInfoAccum = 0.f;
        const UStorySubsystem* Story = UStorySubsystem::Get(this);
        if (!Story || !Story->HasState())
        {
            MenuWidget->SetQuestInfo(TEXT("아직 받은 퀘스트 정보가 없습니다."), FString());
            break;
        }
        const FStoryState& State = Story->GetCurrentState();
        auto TitleOf = [&State](const FString& Id) { const FString* T = State.SideTitles.Find(Id); return T ? *T : Id; };
        FString Side;
        for (const FString& Id : State.Side) Side += FString::Printf(TEXT("• %s\n"), *TitleOf(Id));
        for (const FString& Id : State.AvailableSide) Side += FString::Printf(TEXT("• %s (수락 가능)\n"), *TitleOf(Id));
        MenuWidget->SetQuestInfo(State.QuestLog.IsEmpty() ? TEXT("메인 퀘스트 완료") : State.QuestLog, Side.IsEmpty() ? TEXT("진행 중인 서브퀘스트 없음") : Side.TrimEnd());
        break;
    }
    default:
        break;
    }
}

// ============================================================================
// 설정 — 저장·불러오기·적용
// ============================================================================

void UVRPlayerUIComponent::LoadSettings()
{
    if (UGameplayStatics::DoesSaveGameExist(USettingsSaveGame::SlotName, 0))
    {
        Settings = Cast<USettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(USettingsSaveGame::SlotName, 0));
    }
    if (!Settings)
    {
        Settings = Cast<USettingsSaveGame>(UGameplayStatics::CreateSaveGameObject(USettingsSaveGame::StaticClass()));
    }
    bSettingsDirty = false;

    if (MenuWidget) MenuWidget->SetVolumeValue(Settings->MasterVolume);
    ApplyMasterVolume(Settings->MasterVolume);
}

void UVRPlayerUIComponent::SaveSettings()
{
    if (!Settings || !bSettingsDirty) return;
    UGameplayStatics::SaveGameToSlot(Settings, USettingsSaveGame::SlotName, 0);
    bSettingsDirty = false;
    UE_LOG(LogTemp, Log, TEXT("[Settings] 저장: 마스터 볼륨 %.2f"), Settings->MasterVolume);
}

void UVRPlayerUIComponent::ApplyMasterVolume(float Volume)
{
    // 마스터 서브믹스 출력 볼륨 하나로 모든 소리를 조절한다 — 사운드 클래스·믹스 에셋이 필요 없다.
    static const TCHAR* MasterSubmixPath = TEXT("/Engine/EngineSounds/Submixes/MasterSubmixDefault.MasterSubmixDefault");
    USoundSubmix* Master = LoadObject<USoundSubmix>(nullptr, MasterSubmixPath);
    if (!Master)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Settings] 마스터 서브믹스를 못 찾아 볼륨을 적용하지 못했다: %s"), MasterSubmixPath);
        return;
    }
    Master->SetSubmixOutputVolume(this, Volume);
    UE_LOG(LogTemp, Log, TEXT("[Settings] 마스터 볼륨 %.2f 적용"), Volume);
}

void UVRPlayerUIComponent::OnMenuVolumeChanged(float Volume)
{
    if (!Settings) return;
    Settings->MasterVolume = FMath::Clamp(Volume, 0.f, 1.f);
    bSettingsDirty = true;
    ApplyMasterVolume(Settings->MasterVolume);   // 슬라이더를 움직이는 즉시 소리가 바뀐다
}

bool UVRPlayerUIComponent::PressPointer()
{
    // 인벤토리 열림 중 트리거는 UI 클릭 — 안 막으면 슬롯을 누를 때마다 손앞으로 발사체가 나간다.
    AVRPawn* Pawn = GetPawn();
    if (!(bInventoryOpen || bMenuOpen) || !Pawn || !Interactor) return false;
    Interactor->PressPointerKey(EKeys::LeftMouseButton);
    bPointerPressed = true;
    return true;
}

void UVRPlayerUIComponent::ReleasePointer()
{
    AVRPawn* Pawn = GetPawn();
    if (!bPointerPressed || !Pawn || !Interactor) return;
    Interactor->ReleasePointerKey(EKeys::LeftMouseButton);
    bPointerPressed = false;
}

void UVRPlayerUIComponent::NavigateInventory(const FVector2D& Stick)
{
    UInventoryComponent* Inventory = GetPawn() ? GetPawn()->Inventory : nullptr;
    if (!Inventory || Inventory->InventorySlots.Num() == 0) return;

    // 기울임 임계와 복귀 임계를 따로 둔다 — 하나면 경계에서 떨려 여러 칸이 한 번에 넘어간다.
    const float PushThreshold = 0.6f;
    const float ReleaseThreshold = 0.3f;

    if (!bSlotNavArmed)
    {
        if (FMath::Abs(Stick.X) < ReleaseThreshold && FMath::Abs(Stick.Y) < ReleaseThreshold)
        {
            bSlotNavArmed = true;
        }
        return;
    }

    int32 Step = 0;
    if (FMath::Abs(Stick.X) >= PushThreshold)
    {
        Step = (Stick.X > 0.f) ? 1 : -1;
    }
    else if (FMath::Abs(Stick.Y) >= PushThreshold)
    {
        // 스틱을 위로 = 윗줄 = 인덱스 감소. 그리드가 좌→우, 위→아래로 채워지기 때문.
        Step = (Stick.Y > 0.f) ? -InventoryGridColumns : InventoryGridColumns;
    }
    else
    {
        return;
    }

    bSlotNavArmed = false;
    Inventory->SetSelectedSlot(Inventory->SelectedSlotIndex + Step);

    if (bDebugInventorySelection && GEngine)
    {
        const FInventorySlot& Slot = Inventory->InventorySlots[Inventory->SelectedSlotIndex];
        const FString Label = Slot.IsEmpty()
            ? TEXT("(빈 칸)")
            : FString::Printf(TEXT("%s x%d"), *Slot.ItemData.ItemID, Slot.Count);
        GEngine->AddOnScreenDebugMessage(8811, 2.f, FColor::Cyan,
            FString::Printf(TEXT("[인벤] %d번 슬롯: %s"), Inventory->SelectedSlotIndex, *Label));
    }
}

void UVRPlayerUIComponent::ActivateSelectedSlot(EEquipmentSlot HandSlot)
{
    // 인벤토리를 연 상태의 그립은 "고른 슬롯을 발동한다"는 뜻 — 월드 아이템 줍기와 겹치지 않는다.
    // 종류별 분기(소비=사용 / 장비=장착 / 일반=손에 쥐기)는 ActivateItem 이 들고 있어서 여기서 다시 보지 않는다.
    UInventoryComponent* Inventory = GetPawn() ? GetPawn()->Inventory : nullptr;
    if (!Inventory || !Inventory->InventorySlots.IsValidIndex(Inventory->SelectedSlotIndex)) return;

    const FInventorySlot& Slot = Inventory->InventorySlots[Inventory->SelectedSlotIndex];
    if (Slot.IsEmpty())
    {
        if (bDebugInventorySelection && GEngine)
        {
            GEngine->AddOnScreenDebugMessage(8812, 2.f, FColor::Orange, TEXT("[인벤] 빈 슬롯 — 꺼낼 것 없음"));
        }
        return;
    }

    const FString ItemID = Slot.ItemData.ItemID;
    const bool bActivated = Inventory->ActivateItem(ItemID, HandSlot);
    if (bDebugInventorySelection && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(8812, 2.f, bActivated ? FColor::Green : FColor::Red,
            FString::Printf(TEXT("[인벤] %s %s"), *ItemID, bActivated ? TEXT("발동") : TEXT("발동 실패")));
    }
}

void UVRPlayerUIComponent::DumpInventoryHUD() const
{
    auto Report = [](const FString& Line)
    {
        UE_LOG(LogTemp, Warning, TEXT("[InvDump] %s"), *Line);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, FString::Printf(TEXT("[InvDump] %s"), *Line));
    };

    const AVRPawn* Pawn = GetPawn();
    const UInventoryComponent* Inventory = Pawn ? Pawn->Inventory : nullptr;
    if (!Inventory)
    {
        Report(TEXT("Inventory 컴포넌트 없음"));
        return;
    }

    Report(FString::Printf(TEXT("슬롯 %d개"), Inventory->InventorySlots.Num()));
    for (const FInventorySlot& Slot : Inventory->InventorySlots)
    {
        Report(FString::Printf(TEXT("  - %s x%d"), *Slot.ItemData.ItemID, Slot.Count));
    }

    const UWidgetComponent* HUDWidgetComp = HUDPanel;
    Report(FString::Printf(TEXT("HUDWidget=%s  HUDWidgetComp=%s  visible=%d  open=%d"),
        HUDWidget ? TEXT("OK") : TEXT("NULL"),
        HUDWidgetComp ? TEXT("OK") : TEXT("NULL"),
        HUDWidgetComp ? (HUDWidgetComp->IsVisible() ? 1 : 0) : -1,
        bInventoryOpen ? 1 : 0));

    if (HUDWidget)
    {
        // 위젯이 폰을 못 잡으면 슬롯 조회가 통째로 빈 배열이 된다 — UI 무반응의 주 원인.
        Report(FString::Printf(TEXT("위젯 OwnerPawn=%s  위젯이 본 슬롯 %d개  패널열림=%d"),
            HUDWidget->GetOwningPlayerPawn() ? *HUDWidget->GetOwningPlayerPawn()->GetName() : TEXT("NULL"),
            HUDWidget->GetInventorySlots().Num(),
            HUDWidget->IsInventoryVisible() ? 1 : 0));
    }
}

// ============================================================================
// 패널·채팅·포인터·이름표 — 매 틱
// ============================================================================

void UVRPlayerUIComponent::OpenChat()
{
    AVRPawn* Pawn = GetPawn();
    if (Pawn && ChatPanel) ChatPanel->Open();
}

// 포인터 광선 갱신 — 조준 결과(WidgetInteraction 의 마지막 히트)를 그대로 그린다.
// 별도 트레이스를 또 쏘지 않는 이유: 광선과 실제 클릭 판정이 어긋나면 보이는 곳과 눌리는 곳이 달라진다.
void UVRPlayerUIComponent::UpdatePointerVisual()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn || !PointerBeam || !PointerDot || !Interactor) return;
    UStaticMeshComponent* Beam = PointerBeam;
    UStaticMeshComponent* Dot = PointerDot;

    if (!(bInventoryOpen || bMenuOpen))
    {
        if (Beam->IsVisible()) Beam->SetVisibility(false);
        if (Dot->IsVisible())  Dot->SetVisibility(false);
        return;
    }

    const FHitResult& Hit = Interactor->GetLastHitResult();
    const bool bHit = Hit.bBlockingHit;
    const float Length = bHit ? Hit.Distance : Interactor->InteractionDistance;

    // 원통 기본 크기 100cm(지름 100) 기준 → 실치수/100 이 스케일.
    Beam->SetRelativeScale3D(FVector(PointerBeamThickness / 100.f,
                                     PointerBeamThickness / 100.f,
                                     FMath::Max(Length, 1.f) / 100.f));
    // 원통은 중심 기준이라 절반 지점에 놓아야 손끝에서 히트점까지 정확히 채워진다.
    Beam->SetRelativeLocation(FVector(Length * 0.5f, 0.f, 0.f));
    Beam->SetVisibility(true);

    if (bHit)
    {
        // 히트점은 부모 회전과 무관한 월드 좌표 — 위젯 면에 살짝 띄워 Z-파이팅을 피한다.
        Dot->SetWorldLocation(Hit.ImpactPoint + Hit.ImpactNormal * 0.3f);
    }
    Dot->SetVisibility(bHit);

    // 색 전환은 상태가 바뀌는 프레임에만 — MID 파라미터 쓰기는 매 틱 돌릴 만큼 싸지 않다.
    if (PointerMID && bHit != bPointerWasHitting)
    {
        const FLinearColor C = bHit ? PointerHitColor : PointerColor;
        PointerMID->SetVectorParameterValue(TEXT("Color"), C);
        bPointerWasHitting = bHit;
    }
}

void UVRPlayerUIComponent::UpdateItemTooltip()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn || !ItemTooltip) return;

    // 이미 쥔 물건에는 이름표가 필요 없다 — 손에 든 걸 다시 설명할 이유가 없고,
    // 손을 따라다니는 이름표는 시야만 가린다.
    const UInventoryComponent* Inventory = Pawn->Inventory;
    const bool bHolding = Inventory && (Inventory->GetHeldItem(EEquipmentSlot::MainHand)
                                     || Inventory->GetHeldItem(EEquipmentSlot::OffHand));
    ItemTooltip->ShowForItem(bHolding ? nullptr : Pawn->FindNearestItemNearHand(TooltipRange, /*bLeft=*/false));
}
