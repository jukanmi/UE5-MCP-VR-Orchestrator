#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/Subsystems/ItemManager.h"
#include "Core/Interfaces/Entity.h"
#include "DroppedItemBase.generated.h"

// [의도(Why)] 월드 상에 드랍되어 물리적으로 동작하고, ItemManager에 의해 글로벌하게 추적되는 기본 아이템 블루프린트용 부모 클래스입니다.
UCLASS(Blueprintable, BlueprintType)
class UE5_MCP_VR_API ADroppedItemBase : public AActor, public IItem
{
    GENERATED_BODY()
    
public:    
    ADroppedItemBase();

protected:
    virtual void BeginPlay() override;

#if WITH_EDITOR
    /** 에디터에서 ItemTemplateID 를 바꾸면 그 자리에서 메시를 바꿔 끼운다. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    /** ItemTemplateID 로 DT_ItemRegistry 를 조회해 ItemMesh 를 갱신한다. 실패 시 메시를 건드리지 않는다. */
    void SyncMeshFromItemData();
    
    // 아이템 파괴나 레벨 전환 시 발생할 수 있는 참조 오류를 막으려면 소멸 직전에 제거해야 합니다.
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // 아이템의 시각적 형태 및 물리 연산(Simulate Physics)을 담당하는 코어 메시
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Component")
    class UStaticMeshComponent* ItemMesh;

    // 플레이어 또는 NPC가 근접하여 상호작용하기 편하도록 범위를 넓게 잡아주는 충돌 영역
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Component")
    class USphereComponent* InteractionSphere;

    // 관리 편의성과 데이터 중복 정의를 막기 위해, ItemManager에서 사용하는 기본 데이터 구조체를 그대로 활용합니다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Data")
    FDroppedItemData ItemData;

    // 이 액터 하나가 나타내는 수량. 획득 시 인벤토리에 이 개수만큼 들어갑니다(포션 3개 묶음 등).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Data", meta = (ClampMin = "1"))
    int32 Amount = 1;

    //외부(NPC, Player) 액터가 이 아이템을 획득했을 때 파괴 동작 수동 호출
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    void ConsumeItem();

    /** 이 아이템을 인벤토리로 획득 — 마스터 데이터 조회 → AddItem → 액터 소멸. 실패(미등록 ID·공간/무게 부족)면
     *  액터를 그대로 남겨 다시 시도할 수 있게 하고 false. 플레이어 픽업·NPC 픽업이 같은 경로를 쓴다. */
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    bool TryPickupInto(class UInventoryComponent* Inventory);

    /**
     * 물리·콜리전·상호작용 구체를 한 번에 잠그거나 되살린다. 쥠·거래 접시 잠금 = true, 놓음·던짐·취소 = false.
     * 밖에서 ItemMesh 를 직접 만지지 말 것 — 되살릴 때 프로파일을 다시 지정해야 채널 응답까지 돌아오고,
     * 잠글 때 상호작용 구체까지 꺼야 쥔 물건이 주변 아이템 검색에 다시 걸리지 않는다. 그 지식은 여기 한 곳에만 둔다.
     */
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    void SetPhysicsFrozen(bool bFrozen);

    /**
     * 손에서 놓아 던진다 — 물리·충돌을 되살리고 속도를 실은 뒤, 짧은 창 동안만 타격 판정을 켠다.
     * 데미지 계수는 던진 쪽(VRPawn)의 동역학 튜닝을 그대로 받는다. 근접 스윙과 같은 값으로
     * 맞아야 "같은 손으로 때린 것"의 세기가 무기 종류만으로 갈리기 때문.
     */
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    void LaunchThrown(const FVector& Velocity, AActor* Thrower,
                      float DamageScale, float MaxDamage, float MinSpeedMs);

    /** 던진 뒤 타격 판정이 살아 있는 시간(초). 굴러다니는 아이템이 계속 때리지 않게 한다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Throw", meta = (ClampMin = "0.1"))
    float ThrowDamageWindow = 2.0f;

    /** 거래 접시에 올라가 잠긴 상태 — 손으로 다시 집을 수 없다(수락/취소로만 소유권이 바뀐다).
     *  잠금 없이 두면 올려둔 물건을 쥔 채 취소를 눌러 같은 아이템이 손과 인벤토리에 동시에 남는다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Trade")
    bool bTradeLocked = false;

protected:
    /** 던진 아이템이 무언가에 부딪혔을 때 — 창이 살아 있고 상대가 NPC 면 ½mv² 데미지. */
    UFUNCTION()
    void OnMeshHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                   FVector NormalImpulse, const FHitResult& Hit);

    /** 타격 창 종료 — 던진 사람 참조를 지우는 것이 곧 "이제 안 때린다"는 상태다. */
    void EndThrowWindow();

    /** 던진 액터. 유효하면 타격 창이 살아 있다는 뜻이며, 자기 자신을 때리지 않게 걸러내는 데도 쓴다. */
    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> ThrownBy;

    // 던진 쪽에서 주입받은 동역학 계수 — 근접 스윙과 같은 튜닝을 공유한다.
    float ThrowDamageScale = 1.f;
    float ThrowMaxDamage = 100.f;
    float ThrowMinSpeedMs = 2.f;

    FTimerHandle ThrowWindowTimer;

public:
    // === IItem 인터페이스 구현 ===
    virtual FString GetEntityID_Implementation() const override { return ItemData.ItemInstanceID; }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::Item; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }
    virtual bool IsPickupable_Implementation() const override { return IsValid(this) && !IsPendingKillPending(); }
    virtual FString GetItemID_Implementation() const override { return ItemData.ItemTemplateID; }
};
