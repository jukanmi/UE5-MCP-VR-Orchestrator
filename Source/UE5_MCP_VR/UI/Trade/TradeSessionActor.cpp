#include "UI/Trade/TradeSessionActor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Core/BP/VRPawn.h"
#include "Core/Utils/EngineShapes.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/Subsystems/ItemManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NPC/BP/SmartNPC.h"
#include "UI/BP/ItemTooltipWidget.h"

namespace
{
    // 색이 곧 라벨이다 — 글자를 넣으려면 위젯이 넷 더 필요한데, 초록/빨강이면 설명이 필요 없다.
    void TintMesh(UStaticMeshComponent* Mesh, UObject* Outer, const FLinearColor& Color)
    {
        if (!Mesh) return;
        if (UMaterialInstanceDynamic* MID = EngineShapes::MakeEmissiveMID(Outer, Color))
        {
            Mesh->SetMaterial(0, MID);
        }
    }
}

ATradeSessionActor::ATradeSessionActor()
{
    // 손 근접으로 버튼을 누르므로 틱이 필요하다. 거래가 열려 있는 동안만 사는 액터라 부담은 없다.
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // 접시·버튼은 전부 충돌 없음 — 올리려는 물건이 접시에 부딪혀 튕기면 올리는 동작 자체가 안 된다.
    // 올림·누름 판정은 콜리전이 아니라 거리로 한다.
    PlayerPlate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlayerPlate"));
    PlayerPlate->SetupAttachment(SceneRoot);
    PlayerPlate->SetRelativeLocation(FVector(0.f, -20.f, 0.f));
    PlayerPlate->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.02f));
    PlayerPlate->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    NpcPlate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NpcPlate"));
    NpcPlate->SetupAttachment(SceneRoot);
    NpcPlate->SetRelativeLocation(FVector(0.f, 20.f, 0.f));
    NpcPlate->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.02f));
    NpcPlate->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    AcceptButton = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AcceptButton"));
    AcceptButton->SetupAttachment(SceneRoot);
    AcceptButton->SetRelativeLocation(FVector(-22.f, -10.f, 3.f));
    AcceptButton->SetRelativeScale3D(FVector(0.06f));
    AcceptButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    CancelButton = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CancelButton"));
    CancelButton->SetupAttachment(SceneRoot);
    CancelButton->SetRelativeLocation(FVector(-22.f, 10.f, 3.f));
    CancelButton->SetRelativeScale3D(FVector(0.06f));
    CancelButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    RequestWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("RequestWidget"));
    RequestWidget->SetupAttachment(SceneRoot);
    RequestWidget->SetRelativeLocation(FVector(0.f, 0.f, 25.f));
    RequestWidget->SetWidgetSpace(EWidgetSpace::World);
    RequestWidget->SetDrawSize(FVector2D(400.f, 140.f));
    RequestWidget->SetRelativeScale3D(FVector(0.05f));
    RequestWidget->SetTwoSided(true);
    RequestWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RequestWidget->SetWidgetClass(UItemTooltipWidget::StaticClass());
}

void ATradeSessionActor::BeginPlay()
{
    Super::BeginPlay();

    UStaticMesh* Cube = EngineShapes::LoadCube();
    UStaticMeshComponent* Meshes[] = { PlayerPlate, NpcPlate, AcceptButton, CancelButton };
    for (UStaticMeshComponent* Mesh : Meshes)
    {
        if (Mesh && Cube) Mesh->SetStaticMesh(Cube);
    }

    TintMesh(AcceptButton, this, FLinearColor(0.15f, 0.9f, 0.3f, 1.f));
    TintMesh(CancelButton, this, FLinearColor(0.9f, 0.2f, 0.2f, 1.f));
    TintMesh(PlayerPlate, this, FLinearColor(0.2f, 0.5f, 0.9f, 1.f));
    TintMesh(NpcPlate, this, FLinearColor(0.8f, 0.7f, 0.25f, 1.f));
}

bool ATradeSessionActor::InitSession(ASmartNPC* InNpc, APawn* InPlayer,
                                     const FString& InGiveItemID, int32 InGiveAmount,
                                     const FString& InGetItemID, int32 InGetAmount)
{
    Npc = InNpc;
    Player = InPlayer;
    GiveItemID = InGiveItemID;
    GiveAmount = FMath::Max(InGiveAmount, 0);
    GetItemID = InGetItemID;
    GetAmount = FMath::Max(InGetAmount, 0);

    UInventoryComponent* NpcInv = InNpc ? InNpc->FindComponentByClass<UInventoryComponent>() : nullptr;
    UItemManager* ItemManager = UItemManager::Get(this);
    if (!NpcInv || !ItemManager) return false;

    // NPC 가 내놓을 물건을 지금 인벤토리에서 빼 실물로 만든다. 여기서 빼두지 않으면
    // 거래가 진행되는 동안 같은 물건을 다른 경로(GiveItem 등)로 또 줄 수 있다.
    if (!GiveItemID.IsEmpty() && GiveAmount > 0)
    {
        FItemData GiveData;
        if (!ItemManager->GetItemDataByID(GiveItemID, GiveData)) return false;
        if (NpcInv->GetItemCountInSlots(GiveItemID) < GiveAmount) return false;

        const FTransform PlateTransform(FRotator::ZeroRotator,
                                        NpcPlate->GetComponentLocation() + FVector(0.f, 0.f, 5.f));

        ADroppedItemBase* Offered = NpcInv->SpawnItemActor(GiveData, GiveItemID, PlateTransform, GiveAmount);
        if (!Offered) return false;

        if (!NpcInv->RemoveItem(GiveItemID, GiveAmount))
        {
            // 차감이 안 되면 실물을 남기지 않는다 — 남기는 순간 그대로 복제다.
            Offered->Destroy();
            return false;
        }

        SetItemLocked(Offered, true);
        NpcOffered.Add(Offered);
    }

    // 요구 품목 안내 — 아이템 이름표 위젯이 이미 이름·종류·수량을 그리므로 그대로 쓴다.
    if (!GetItemID.IsEmpty())
    {
        FItemData GetData;
        if (ItemManager->GetItemDataByID(GetItemID, GetData))
        {
            RequestWidget->InitWidget();
            if (UItemTooltipWidget* W = Cast<UItemTooltipWidget>(RequestWidget->GetUserWidgetObject()))
            {
                W->SetItem(GetData, GetAmount);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Trade] 세션 개시 — %s 가 %s x%d 를 내놓고 %s x%d 를 요구"),
           InNpc ? *InNpc->AgentID : TEXT("?"), *GiveItemID, GiveAmount, *GetItemID, GetAmount);
    return true;
}

void ATradeSessionActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    CheckHandPress();

    // 안내 위젯은 플레이어를 향한다 — 위젯의 가시면은 +X.
    if (RequestWidget && Player.IsValid())
    {
        const FVector ToPlayer = Player->GetActorLocation() - RequestWidget->GetComponentLocation();
        if (!ToPlayer.IsNearlyZero())
        {
            RequestWidget->SetWorldRotation(ToPlayer.Rotation());
        }
    }
}

void ATradeSessionActor::CheckHandPress()
{
    AVRPawn* VRPlayer = Cast<AVRPawn>(Player.Get());
    if (!VRPlayer || !AcceptButton || !CancelButton) return;

    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    if (Now - LastPressTime < ButtonCooldown) return;

    const FVector Hands[2] = { VRPlayer->GetHandLocation(true), VRPlayer->GetHandLocation(false) };
    const float RadiusSq = ButtonPressRadius * ButtonPressRadius;

    for (const FVector& Hand : Hands)
    {
        if (FVector::DistSquared(Hand, AcceptButton->GetComponentLocation()) <= RadiusSq)
        {
            LastPressTime = Now;
            Accept();
            return;
        }
        if (FVector::DistSquared(Hand, CancelButton->GetComponentLocation()) <= RadiusSq)
        {
            LastPressTime = Now;
            Cancel();
            return;
        }
    }
}

bool ATradeSessionActor::TrySnapItem(ADroppedItemBase* Item)
{
    if (!IsValid(Item) || !PlayerPlate) return false;

    const FVector PlateLoc = PlayerPlate->GetComponentLocation();
    if (FVector::Dist(Item->GetActorLocation(), PlateLoc) > PlateRadius) return false;

    // 접시 위로 조금씩 쌓는다 — 물리를 끄므로 겹쳐도 튕기지 않는다.
    const float StackHeight = 5.f + 4.f * PlayerOffered.Num();
    Item->SetActorLocation(PlateLoc + FVector(0.f, 0.f, StackHeight));
    SetItemLocked(Item, true);
    PlayerOffered.Add(Item);

    UE_LOG(LogTemp, Log, TEXT("[Trade] 접시에 올림 — %s x%d"), *Item->ItemData.ItemTemplateID, Item->Amount);
    return true;
}

bool ATradeSessionActor::Accept()
{
    ASmartNPC* TargetNpc = Npc.Get();
    UInventoryComponent* NpcInv = TargetNpc ? TargetNpc->FindComponentByClass<UInventoryComponent>() : nullptr;
    UInventoryComponent* PlayerInv = Player.IsValid() ? Player->FindComponentByClass<UInventoryComponent>() : nullptr;

    UItemManager* ItemManager = UItemManager::Get(this);
    if (!NpcInv || !PlayerInv || !ItemManager) return false;

    // 요구 수량 검사 — 모자라면 아무것도 옮기지 않고 세션을 유지한다(더 올릴 수 있게).
    if (!GetItemID.IsEmpty())
    {
        int32 OfferedCount = 0;
        for (const TObjectPtr<ADroppedItemBase>& Item : PlayerOffered)
        {
            if (IsValid(Item) && Item->ItemData.ItemTemplateID == GetItemID)
            {
                OfferedCount += Item->Amount;
            }
        }
        if (OfferedCount < GetAmount)
        {
            UE_LOG(LogTemp, Log, TEXT("[Trade] 수락 거부 — %s %d/%d"), *GetItemID, OfferedCount, GetAmount);
            return false;
        }
    }

    // 플레이어 → NPC. 수령 실패분은 파괴하지 않고 잠금만 풀어 바닥에 남긴다(증발보다 낫다).
    for (const TObjectPtr<ADroppedItemBase>& Item : PlayerOffered)
    {
        if (!IsValid(Item)) continue;

        FItemData Data;
        if (ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, Data)
            && NpcInv->AddItem(Data, Item->Amount))
        {
            Item->Destroy();
        }
        else
        {
            SetItemLocked(Item, false);
        }
    }

    // NPC → 플레이어. 인벤이 꽉 차면 마찬가지로 바닥에 남긴다.
    for (const TObjectPtr<ADroppedItemBase>& Item : NpcOffered)
    {
        if (!IsValid(Item)) continue;

        FItemData Data;
        if (ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, Data)
            && PlayerInv->AddItem(Data, Item->Amount))
        {
            Item->Destroy();
        }
        else
        {
            SetItemLocked(Item, false);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Trade] 성사 — 세션 종료"));
    Destroy();
    return true;
}

void ATradeSessionActor::Cancel()
{
    // 인벤토리로 되돌리지 않는다 — 되돌리는 순간 "손에 쥔 채 취소" 복사 경로가 생긴다.
    // 물건은 그 자리에서 떨어지고, 원하면 다시 주우면 된다.
    ReleaseAll();
    UE_LOG(LogTemp, Log, TEXT("[Trade] 취소 — 올려둔 물건을 바닥에 남기고 종료"));
    Destroy();
}

void ATradeSessionActor::ReleaseAll()
{
    for (const TObjectPtr<ADroppedItemBase>& Item : PlayerOffered)
    {
        SetItemLocked(Item, false);
    }
    for (const TObjectPtr<ADroppedItemBase>& Item : NpcOffered)
    {
        SetItemLocked(Item, false);
    }
    PlayerOffered.Reset();
    NpcOffered.Reset();
}

void ATradeSessionActor::SetItemLocked(ADroppedItemBase* Item, bool bLocked)
{
    if (!IsValid(Item)) return;

    Item->bTradeLocked = bLocked;
    Item->SetPhysicsFrozen(bLocked);
}
