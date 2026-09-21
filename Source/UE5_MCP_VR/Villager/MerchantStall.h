#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MerchantStall.generated.h"

class AVillagerCharacter;
class ADroppedItemBase;
class UBoxComponent;
class UInventoryComponent;
class UStaticMeshComponent;
struct FItemData;

/**
 * 상인 가판대 — 진열 슬롯 6 + 매입 상자. LLM·서버 0.
 * 재고는 Merchant(AVillagerCharacter)의 Inventory. 진열 = 재고 1개를 실물(ADroppedItemBase, 진열 상태)로 슬롯에 세움.
 * 구매(TryPurchase)는 VRPawn 그랩 시작에서, 매입(TrySell)은 그랩 해제 위치가 BuyBox 안일 때 호출된다.
 * 가격 = ItemRegistry BaseValue, 매입가 = Max(1, BaseValue×0.5). 상인 골드는 무한.
 * 빈 슬롯은 RestockInterval 마다 RestockItems(비면 재고 InitialDefaultItems)에서 1개씩 다시 채운다.
 */
UCLASS(Blueprintable, BlueprintType)
class UE5_MCP_VR_API AMerchantStall : public AActor
{
    GENERATED_BODY()

public:
    AMerchantStall();

    /** 재고·대사를 가진 상인. build_story_scene.py 가 배치 후 지정. 없으면 가판대는 빈 채로 선다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant")
    TObjectPtr<AVillagerCharacter> Merchant;

    /** 진열 슬롯 위치(액터 로컬). 기본 3×2, 탁자 위 높이 SlotOffsets.Z. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant")
    TArray<FVector> SlotOffsets;

    /** 매입 상자 — 이 안에서 그랩을 놓으면 판매 판정. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Merchant")
    TObjectPtr<UBoxComponent> BuyBox;

    /** 매입 상자 외형(선택, build_story_scene.py 가 메시 지정). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Merchant")
    TObjectPtr<UStaticMeshComponent> BuyBoxMesh;

    /** 빈 슬롯 재입고 주기(초). 0 이하면 재입고 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant", meta = (ClampMin = "0"))
    float RestockInterval = 600.f;

    /** 재입고 후보 ItemID(순환). 비어 있으면 상인 Inventory 의 InitialDefaultItems 를 쓴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant")
    TArray<FString> RestockItems;

    // --- 상인 대사 ({0}{1} = FString::Format 인자) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant|Lines")
    FString NotEnoughGoldLine = TEXT("은화가 모자라. {0} 골드짜리인데 자넨 {1} 골드뿐이잖나.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant|Lines")
    FString BagFullLine = TEXT("가방이 꽉 찼군. 자리부터 비우고 오게.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant|Lines")
    FString SoldLine = TEXT("{0} 골드일세. 고맙네.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant|Lines")
    FString BoughtLine = TEXT("{0} 골드 쳐주지. 잘 쓰겠네.");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Merchant|Lines")
    FString RefuseBuyLine = TEXT("그건 안 사네. 값을 매길 물건이 아니야.");

    /**
     * 구매 — 진열품 그랩 시작. Transactional: CanAddItem → RemoveGold → 진열 해제(호출측이 손에 붙인다).
     * 실패(내 진열품 아님·가방·골드 부족)면 진열 그대로, 상인 대사, false. 성공하면 그 슬롯을 재고에서 재진열.
     */
    UFUNCTION(BlueprintCallable, Category = "Merchant")
    bool TryPurchase(ADroppedItemBase* Item, UInventoryComponent* Buyer);

    /**
     * 매입 — 매입 상자 안에서 놓은 물건. 진열품·BaseValue 0 은 거부(물건은 상자 안에 그대로).
     * 성공: 매입가×Amount 골드 지급, 재고 AddItem, 실물 파괴, 빈 슬롯 채움.
     */
    UFUNCTION(BlueprintCallable, Category = "Merchant")
    bool TrySell(ADroppedItemBase* Item, UInventoryComponent* Seller);

    UFUNCTION(BlueprintPure, Category = "Merchant")
    bool IsInBuyBox(const FVector& WorldLocation) const;

    /** 이 위치를 매입 상자에 품은 가판대(없으면 nullptr). 가판대는 한 손에 꼽히므로 놓는 순간에만 훑는다. */
    static AMerchantStall* FindStallContaining(const UObject* WorldContext, const FVector& WorldLocation);

    /** 빈 슬롯마다 재고에서 1개 꺼내 진열. 재고가 비면 그 슬롯은 빈 채로 둔다. */
    UFUNCTION(BlueprintCallable, Category = "Merchant")
    void FillEmptySlots();

    /** 매입가 = Max(1, BaseValue×0.5) (BaseValue ≤ 0 이면 0 = 매입 거부). */
    static int32 BuyPriceFor(const FItemData& Data);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** 슬롯별 진열 실물. 구매·파괴로 비면 무효 포인터. */
    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<ADroppedItemBase>> Displayed;

    /** 재입고 순환 커서. */
    int32 RestockCursor = 0;

    FTimerHandle RestockTimer;

    UInventoryComponent* GetStock() const;
    void Say(const FString& Line) const;

    /** 재고 첫 비어 있지 않은 슬롯 1개를 꺼내 SlotIndex 자리에 진열. 재고 없음·스폰 실패면 false. */
    bool DisplayFromStock(int32 SlotIndex);

    /** 빈 슬롯마다 RestockItems 1개를 재고에 넣고 진열. 진열이 다 차 있으면 아무것도 안 한다. */
    void Restock();
};
