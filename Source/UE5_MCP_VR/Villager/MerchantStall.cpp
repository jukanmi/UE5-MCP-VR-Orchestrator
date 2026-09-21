#include "Villager/MerchantStall.h"
#include "Villager/VillagerCharacter.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/BP/ItemDataAsset.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/Subsystems/ItemManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"

AMerchantStall::AMerchantStall()
{
    PrimaryActorTick.bCanEverTick = false;  // 전부 이벤트(그랩)·타이머(재입고)

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    // 매입 상자 — 판정은 놓는 순간 IsInBuyBox 점 검사라 오버랩 이벤트를 켜지 않는다.
    // 쥔 물건은 콜리전이 꺼져 있어 오버랩이 안 오고, 진열품은 애초에 이 상자를 무시해야 한다(꼼수 매입 차단).
    BuyBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BuyBox"));
    BuyBox->SetupAttachment(RootComponent);
    BuyBox->SetRelativeLocation(FVector(250.f, 0.f, 40.f));  // 탁자 옆(+X)
    BuyBox->SetBoxExtent(FVector(45.f, 45.f, 40.f));
    BuyBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BuyBox->SetGenerateOverlapEvents(false);

    BuyBoxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuyBoxMesh"));
    BuyBoxMesh->SetupAttachment(BuyBox);
    BuyBoxMesh->SetRelativeLocation(FVector(0.f, 0.f, -40.f));  // Shape_Cube 피벗이 바닥 — 판정 박스와 겹치게 내린다
    BuyBoxMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

    // 3×2, SM_TableRound(반지름 61·상판 70) 위. 탁자가 달라지면 build_story_scene.py 가 덮어쓴다.
    SlotOffsets = {
        FVector(-38.f, -22.f, 80.f), FVector(0.f, -22.f, 80.f), FVector(38.f, -22.f, 80.f),
        FVector(-38.f,  22.f, 80.f), FVector(0.f,  22.f, 80.f), FVector(38.f,  22.f, 80.f),
    };
}

void AMerchantStall::BeginPlay()
{
    Super::BeginPlay();
    Displayed.SetNum(SlotOffsets.Num());

    // 상인 Inventory 의 InitialDefaultItems 는 그쪽 BeginPlay 에서 지급된다 — 액터 BeginPlay 순서가
    // 보장되지 않으므로 한 틱 뒤에 첫 진열. 여기서 바로 채우면 재고 0 으로 보고 빈 가판대가 된다.
    GetWorldTimerManager().SetTimerForNextTick(this, &AMerchantStall::FillEmptySlots);

    if (RestockInterval > 0.f)
    {
        GetWorldTimerManager().SetTimer(RestockTimer, this, &AMerchantStall::Restock, RestockInterval, true);
    }
}

void AMerchantStall::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(RestockTimer);
    Super::EndPlay(EndPlayReason);
}

UInventoryComponent* AMerchantStall::GetStock() const
{
    return IsValid(Merchant) ? Merchant->Inventory : nullptr;
}

void AMerchantStall::Say(const FString& Line) const
{
    if (IsValid(Merchant)) Merchant->Say(Line);
}

int32 AMerchantStall::BuyPriceFor(const FItemData& Data)
{
    // 퀘스트품 등 BaseValue 0 은 매입 거부(0). 그 외엔 반값 내림, 최소 1골드(BaseValue 1 이 0골드가 되지 않게).
    if (Data.BaseValue <= 0) return 0;
    return FMath::Max(1, FMath::FloorToInt32(Data.BaseValue * 0.5f));
}

bool AMerchantStall::IsInBuyBox(const FVector& WorldLocation) const
{
    if (!BuyBox) return false;
    const FVector Local = BuyBox->GetComponentTransform().InverseTransformPosition(WorldLocation);
    const FVector Ext = BuyBox->GetUnscaledBoxExtent();
    return FMath::Abs(Local.X) <= Ext.X && FMath::Abs(Local.Y) <= Ext.Y && FMath::Abs(Local.Z) <= Ext.Z;
}

AMerchantStall* AMerchantStall::FindStallContaining(const UObject* WorldContext, const FVector& WorldLocation)
{
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (TActorIterator<AMerchantStall> It(World); It; ++It)
    {
        if (It->IsInBuyBox(WorldLocation)) return *It;
    }
    return nullptr;
}

bool AMerchantStall::DisplayFromStock(int32 SlotIndex)
{
    UInventoryComponent* Stock = GetStock();
    UItemManager* ItemManager = UItemManager::Get(this);
    if (!Stock || !ItemManager || !SlotOffsets.IsValidIndex(SlotIndex)) return false;

    const FInventorySlot* Src = Stock->InventorySlots.FindByPredicate([](const FInventorySlot& S) { return !S.IsEmpty(); });
    if (!Src) return false;

    FItemData Data;
    const FString ItemID = Src->ItemData.ItemID;
    if (!ItemManager->GetItemDataByID(ItemID, Data)) return false;

    const FTransform SlotTf(GetActorRotation(), GetActorTransform().TransformPosition(SlotOffsets[SlotIndex]));
    ADroppedItemBase* Item = Stock->SpawnItemActor(Data, ItemID, SlotTf, 1);
    if (!Item) return false;

    // 스폰 충돌 조정으로 밀렸을 수 있으니 자리를 다시 못 박고 진열 상태로.
    Item->SetActorTransform(SlotTf);
    Item->SetDisplayed(true, this, Data.BaseValue);
    Displayed[SlotIndex] = Item;

    // 실물이 곧 재고 1개 — 진열 시점에 차감(구매 실패해도 실물은 남으므로 증발 없음).
    Stock->RemoveItem(ItemID, 1);
    return true;
}

void AMerchantStall::FillEmptySlots()
{
    for (int32 i = 0; i < Displayed.Num(); ++i)
    {
        if (Displayed[i].IsValid()) continue;
        if (!DisplayFromStock(i)) break;  // 재고 없음 — 나머지도 빈 채로
    }
}

void AMerchantStall::Restock()
{
    UInventoryComponent* Stock = GetStock();
    UItemManager* ItemManager = UItemManager::Get(this);
    if (!Stock || !ItemManager) return;

    const TArray<FString>& Pool = RestockItems.Num() > 0 ? RestockItems : Stock->InitialDefaultItems;
    if (Pool.Num() == 0) return;

    // 빈 슬롯 수만큼만 재고를 늘린다 — 진열이 다 차 있으면 재고가 무한히 불어나지 않는다.
    int32 Added = 0;
    for (const TWeakObjectPtr<ADroppedItemBase>& Slot : Displayed)
    {
        if (Slot.IsValid()) continue;
        FItemData Data;
        const FString& ItemID = Pool[RestockCursor++ % Pool.Num()];
        if (ItemManager->GetItemDataByID(ItemID, Data) && Stock->AddItem(Data, 1, /*bCheckWeight=*/false)) ++Added;
    }
    if (Added > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[MerchantStall] 재입고 %d개 → 진열"), Added);
        FillEmptySlots();
    }
}

bool AMerchantStall::TryPurchase(ADroppedItemBase* Item, UInventoryComponent* Buyer)
{
    UItemManager* ItemManager = UItemManager::Get(this);
    if (!IsValid(Item) || !Buyer || !ItemManager) return false;
    if (!Item->bIsDisplayed || Item->DisplayStall.Get() != this) return false;

    FItemData Data;
    if (!ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, Data)) return false;

    const int32 Price = Item->DisplayPrice;
    const int32 Amount = FMath::Max(1, Item->Amount);

    // Transactional: 받을 수 있나 → 골드 차감 → 지급(진열 해제, 호출측이 손에 붙인다). 순서가 바뀌면 골드만 사라지거나 공짜가 된다.
    if (!Buyer->CanAddItem(Data, Amount))
    {
        Say(BagFullLine);
        return false;
    }
    if (!Buyer->CanAfford(Price) || !Buyer->RemoveGold(Price))
    {
        Say(FString::Format(*NotEnoughGoldLine, { Price, Buyer->GetGold() }));
        return false;
    }

    const int32 SlotIndex = Displayed.IndexOfByPredicate([Item](const TWeakObjectPtr<ADroppedItemBase>& W) { return W.Get() == Item; });
    if (SlotIndex != INDEX_NONE) Displayed[SlotIndex].Reset();

    Item->SetDisplayed(false);
    Say(FString::Format(*SoldLine, { Price }));
    UE_LOG(LogTemp, Log, TEXT("[MerchantStall] 판매: %s %d골드 → 잔액 %d"), *Data.ItemID, Price, Buyer->GetGold());

    // 같은 슬롯에 다음 재고 재진열(재고가 남았을 때만).
    if (SlotIndex != INDEX_NONE) DisplayFromStock(SlotIndex);
    return true;
}

bool AMerchantStall::TrySell(ADroppedItemBase* Item, UInventoryComponent* Seller)
{
    UItemManager* ItemManager = UItemManager::Get(this);
    if (!IsValid(Item) || !Seller || !ItemManager) return false;

    // 진열품을 매입 상자에 넣는 꼼수 — 상자는 진열 상태를 아예 보지 않는다.
    if (Item->bIsDisplayed || Item->bTradeLocked) return false;

    FItemData Data;
    if (!ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, Data)) return false;

    const int32 Unit = BuyPriceFor(Data);
    if (Unit <= 0)
    {
        Say(RefuseBuyLine);
        return false;
    }

    const int32 Amount = FMath::Max(1, Item->Amount);
    const int32 Total = Unit * Amount;

    // 재고에 못 들어가도(슬롯 포화) 매입은 성립 — 상인 골드는 무한이고 물건은 "팔려 나간" 것으로 친다.
    if (UInventoryComponent* Stock = GetStock())
    {
        if (!Stock->AddItem(Data, Amount, /*bCheckWeight=*/false))
        {
            UE_LOG(LogTemp, Log, TEXT("[MerchantStall] 재고 포화 — %s x%d 매입은 성립, 재고엔 미반영"), *Data.ItemID, Amount);
        }
    }
    Seller->AddGold(Total);
    Say(FString::Format(*BoughtLine, { Total }));
    UE_LOG(LogTemp, Log, TEXT("[MerchantStall] 매입: %s x%d = %d골드 → 잔액 %d"), *Data.ItemID, Amount, Total, Seller->GetGold());

    Item->ConsumeItem();
    FillEmptySlots();  // 매입품이 빈 슬롯의 진열 후보가 된다
    return true;
}
