// TradeSessionActor — NPC 와의 물물교환 테이블.
//
// 롤백(트랜잭션) 개념을 쓰지 않는다. 접시에 올라간 물건은 그 즉시 인벤토리에서 빠져나온
// 독립 물리 액터이고, 수락/취소는 그 액터의 소유권만 옮긴다. 가상 상태와 물리 액터를
// 동기화하려 들면 올려둔 물건을 손으로 다시 집는 순간 복사 버그가 난다.
//
// 스폰 경로: NPCActionComponent::ExecuteTrade (LLM 이 Trade 액션을 낼 때만).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TradeSessionActor.generated.h"

class ADroppedItemBase;
class ASmartNPC;
class UBoxComponent;
class UStaticMeshComponent;
class UWidgetComponent;

UCLASS()
class UE5_MCP_VR_API ATradeSessionActor : public AActor
{
    GENERATED_BODY()

public:
    ATradeSessionActor();

    /**
     * 거래 개시 — NPC 가 내놓을 물건을 인벤토리에서 빼 NPC 접시에 실물로 올리고,
     * 플레이어가 올려야 할 품목을 안내 위젯에 띄운다.
     * NPC 보유분이 모자라면 세션을 열지 않고 false.
     */
    bool InitSession(ASmartNPC* InNpc, APawn* InPlayer,
                     const FString& InGiveItemID, int32 InGiveAmount,
                     const FString& InGetItemID, int32 InGetAmount);

    /** 플레이어 접시 범위 안이면 물건을 잠가 올린다. 손에서 놓는 순간 VRPawn 이 물어본다. */
    bool TrySnapItem(ADroppedItemBase* Item);

    /** 요구 수량이 채워졌으면 양쪽 소유권을 옮기고 세션 종료. 모자라면 로그만 남기고 유지. */
    UFUNCTION(BlueprintCallable, Category = "Trade")
    bool Accept();

    /** 취소 — 올려둔 물건의 잠금만 풀어 바닥에 떨어뜨린다. 인벤토리로 되돌리지 않는다. */
    UFUNCTION(BlueprintCallable, Category = "Trade")
    void Cancel();

    /** 접시 크기(cm) — 이 반경 안에서 놓으면 올라간다. */
    UPROPERTY(EditAnywhere, Category = "Trade")
    float PlateRadius = 25.f;

    /** 버튼이 눌리는 손 거리(cm). */
    UPROPERTY(EditAnywhere, Category = "Trade")
    float ButtonPressRadius = 8.f;

    /** 한 번 누른 뒤 다시 먹기까지의 간격(초) — 손이 머무는 동안 연타되는 것을 막는다. */
    UPROPERTY(EditAnywhere, Category = "Trade")
    float ButtonCooldown = 0.8f;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, Category = "Trade")
    USceneComponent* SceneRoot;

    /** 플레이어가 물건을 올리는 접시. */
    UPROPERTY(VisibleAnywhere, Category = "Trade")
    UStaticMeshComponent* PlayerPlate;

    /** NPC 가 내놓은 물건이 놓이는 접시. */
    UPROPERTY(VisibleAnywhere, Category = "Trade")
    UStaticMeshComponent* NpcPlate;

    UPROPERTY(VisibleAnywhere, Category = "Trade")
    UStaticMeshComponent* AcceptButton;

    UPROPERTY(VisibleAnywhere, Category = "Trade")
    UStaticMeshComponent* CancelButton;

    /** 요구 품목 안내 — 아이템 이름표 위젯을 그대로 재사용한다(이름·종류·수량이 그 위젯의 내용 그대로). */
    UPROPERTY(VisibleAnywhere, Category = "Trade")
    UWidgetComponent* RequestWidget;

private:
    /** 손이 버튼 반경에 들어왔는지 검사하고 눌린 것을 실행. */
    void CheckHandPress();

    /** 접시 위 물건 잠금·해제 공통 처리. */
    static void SetItemLocked(ADroppedItemBase* Item, bool bLocked);

    /** 세션이 들고 있는 물건 전부의 잠금을 풀고 액터를 정리 없이 남긴다(취소 경로). */
    void ReleaseAll();

    UPROPERTY(Transient) TWeakObjectPtr<ASmartNPC> Npc;
    UPROPERTY(Transient) TWeakObjectPtr<APawn> Player;

    /** NPC 가 내놓은 실물(에스크로) — 수락 시 플레이어 인벤토리로, 취소 시 바닥으로. */
    UPROPERTY(Transient) TArray<TObjectPtr<ADroppedItemBase>> NpcOffered;

    /** 플레이어가 접시에 올린 실물. */
    UPROPERTY(Transient) TArray<TObjectPtr<ADroppedItemBase>> PlayerOffered;

    FString GiveItemID;
    int32 GiveAmount = 0;
    FString GetItemID;
    int32 GetAmount = 0;

    float LastPressTime = -100.f;
};
