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
#include "UI/BP/ChatWidget.h"
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

void UVRPlayerUIComponent::Init()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn) return;

    // 포인터 비주얼 에셋 — 엔진 기본 도형 + 이미시브 머티리얼. 프로젝트 에셋을 만들지 않으려는 선택으로,
    // 셋 중 하나라도 없으면 포인터만 조용히 안 보이고 클릭 기능 자체는 그대로 동작한다.
    if (Pawn->PointerBeam && Pawn->PointerDot)
    {
        if (UStaticMesh* Cylinder = EngineShapes::LoadCylinder()) Pawn->PointerBeam->SetStaticMesh(Cylinder);
        if (UStaticMesh* Sphere   = EngineShapes::LoadSphere())   Pawn->PointerDot->SetStaticMesh(Sphere);

        // 빔·점이 한 MID 를 공유 — 조준 적중 시 색을 한 번에 바꾼다.
        PointerMID = EngineShapes::MakeEmissiveMID(this, PointerColor);
        if (PointerMID)
        {
            Pawn->PointerBeam->SetMaterial(0, PointerMID);
            Pawn->PointerDot->SetMaterial(0, PointerMID);
        }

        // 굵기·크기는 여기서 한 번만. 길이(Z)는 매 Tick 조준 거리로 덮어쓴다.
        Pawn->PointerDot->SetRelativeScale3D(FVector(PointerDotSize / 100.f));
    }

    // HUD·채팅 위젯 생성 — 로컬 플레이어 컨트롤러일 때만.
    // 위젯은 뷰포트가 아니라 왼손 패널(HUDWidgetComp)에 실린다. 화면 공간 위젯은
    // 스테레오 렌더 타깃 위에 한 번만 합성되어 한쪽 눈에만 보이기 때문.
    APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    if (PC && PC->IsLocalController())
    {
        if (HUDWidgetClass && Pawn->HUDWidgetComp)
        {
            Pawn->HUDWidgetComp->SetOwnerPlayer(PC->GetLocalPlayer());
            Pawn->HUDWidgetComp->SetWidgetClass(HUDWidgetClass);
            Pawn->HUDWidgetComp->InitWidget();
            HUDWidget = Cast<UPlayerHUDWidget>(Pawn->HUDWidgetComp->GetUserWidgetObject());
        }
        if (ChatWidgetClass && Pawn->ChatWidgetComp)
        {
            Pawn->ChatWidgetComp->SetOwnerPlayer(PC->GetLocalPlayer());
            Pawn->ChatWidgetComp->SetWidgetClass(ChatWidgetClass);
            Pawn->ChatWidgetComp->InitWidget();
            ChatWidget = Cast<UChatWidget>(Pawn->ChatWidgetComp->GetUserWidgetObject());
        }
    }

    // HUDWidgetComp 가 블루프린트 직렬화 캐시 등으로 비활성화되어 있는 경우 방어
    if (Pawn->HUDWidgetComp)
    {
        Pawn->HUDWidgetComp->SetVisibility(true);
    }

    // 인벤토리는 닫힌 상태로 시작 — 포인터를 끄고 슬롯을 접는다.
    // 패널 자체는 계속 켜져 있다(HP·스태미나 게이지가 실려 있음).
    ApplyInventoryPresentation(false);
}

void UVRPlayerUIComponent::Update(float /*DeltaTime*/)
{
    UpdatePointerVisual();
    UpdateItemTooltip();
}

// ============================================================================
// 인벤토리 패널
// ============================================================================

void UVRPlayerUIComponent::ToggleInventory()
{
    if (!HUDWidget) return;
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
    if (Pawn->HUDWidgetComp) Pawn->HUDWidgetComp->bForceOpaque = bOpen;
    if (UWidgetInteractionComponent* Interactor = Pawn->HUDInteractor)
    {
        // 닫을 때 눌린 채로 두면 다음에 열었을 때 첫 클릭이 씹힌다.
        if (!bOpen && bPointerPressed)
        {
            Interactor->ReleasePointerKey(EKeys::LeftMouseButton);
            bPointerPressed = false;
        }
        Interactor->SetActive(bOpen);
        Interactor->SetVisibility(bOpen);
    }

    // 닫는 프레임에 바로 끈다 — Tick 을 기다리면 한 프레임 광선이 남는다.
    if (!bOpen)
    {
        if (Pawn->PointerBeam) Pawn->PointerBeam->SetVisibility(false);
        if (Pawn->PointerDot)  Pawn->PointerDot->SetVisibility(false);
    }
}

bool UVRPlayerUIComponent::PressPointer()
{
    // 인벤토리 열림 중 트리거는 UI 클릭 — 안 막으면 슬롯을 누를 때마다 손앞으로 발사체가 나간다.
    AVRPawn* Pawn = GetPawn();
    if (!bInventoryOpen || !Pawn || !Pawn->HUDInteractor) return false;
    Pawn->HUDInteractor->PressPointerKey(EKeys::LeftMouseButton);
    bPointerPressed = true;
    return true;
}

void UVRPlayerUIComponent::ReleasePointer()
{
    AVRPawn* Pawn = GetPawn();
    if (!bPointerPressed || !Pawn || !Pawn->HUDInteractor) return;
    Pawn->HUDInteractor->ReleasePointerKey(EKeys::LeftMouseButton);
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

    const UWidgetComponent* HUDWidgetComp = Pawn->HUDWidgetComp;
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
    if (Pawn && Pawn->ChatWidgetComp) Pawn->ChatWidgetComp->Open();
}

// 포인터 광선 갱신 — 조준 결과(WidgetInteraction 의 마지막 히트)를 그대로 그린다.
// 별도 트레이스를 또 쏘지 않는 이유: 광선과 실제 클릭 판정이 어긋나면 보이는 곳과 눌리는 곳이 달라진다.
void UVRPlayerUIComponent::UpdatePointerVisual()
{
    AVRPawn* Pawn = GetPawn();
    if (!Pawn || !Pawn->PointerBeam || !Pawn->PointerDot || !Pawn->HUDInteractor) return;
    UStaticMeshComponent* Beam = Pawn->PointerBeam;
    UStaticMeshComponent* Dot = Pawn->PointerDot;

    if (!bInventoryOpen)
    {
        if (Beam->IsVisible()) Beam->SetVisibility(false);
        if (Dot->IsVisible())  Dot->SetVisibility(false);
        return;
    }

    const FHitResult& Hit = Pawn->HUDInteractor->GetLastHitResult();
    const bool bHit = Hit.bBlockingHit;
    const float Length = bHit ? Hit.Distance : Pawn->HUDInteractor->InteractionDistance;

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
    if (!Pawn || !Pawn->ItemTooltipComp) return;

    // 이미 쥔 물건에는 이름표가 필요 없다 — 손에 든 걸 다시 설명할 이유가 없고,
    // 손을 따라다니는 이름표는 시야만 가린다.
    const UInventoryComponent* Inventory = Pawn->Inventory;
    const bool bHolding = Inventory && (Inventory->GetHeldItem(EEquipmentSlot::MainHand)
                                     || Inventory->GetHeldItem(EEquipmentSlot::OffHand));
    Pawn->ItemTooltipComp->ShowForItem(bHolding ? nullptr : Pawn->FindNearestItemNearHand(TooltipRange, /*bLeft=*/false));
}
