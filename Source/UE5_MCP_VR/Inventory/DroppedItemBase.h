#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemManager.h"
#include "../Core/Entity.h"   // IItemEntity 계층 인터페이스
#include "DroppedItemBase.generated.h"

// [의도(Why)] 월드 상에 드랍되어 물리적으로 동작하고, ItemManager에 의해 글로벌하게 추적되는 기본 아이템 블루프린트용 부모 클래스입니다.
UCLASS(Blueprintable, BlueprintType)
class UE5_MCP_VR_API ADroppedItemBase : public AActor, public IItemEntity
{
    GENERATED_BODY()
    
public:    
    ADroppedItemBase();

protected:
    virtual void BeginPlay() override;
    
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

    //외부(NPC, Player) 액터가 이 아이템을 획득했을 때 파괴 동작 수동 호출
    UFUNCTION(BlueprintCallable, Category = "Item|Action")
    void ConsumeItem();

    // === IEntity / IItemEntity 인터페이스 구현 ===
    virtual FString GetEntityID_Implementation() const override { return ItemData.ItemInstanceID; }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::Item; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }
    virtual bool IsPickupable_Implementation() const override { return IsValid(this) && !IsPendingKillPending(); }
    virtual FString GetItemID_Implementation() const override { return ItemData.ItemTemplateID; }
};
