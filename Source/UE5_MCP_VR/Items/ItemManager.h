#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ItemManager.generated.h"

// 월드 상에 드랍된 아이템의 식별 정보와 실제 포인터를 묶어 MCP(Python)로의 상태 동기화 및 쿼리를 용이하게 합니다.
USTRUCT(BlueprintType)
struct FDroppedItemData
{
    GENERATED_BODY()

    // 고유 식별자 (UUID). FGuid::NewGuid().ToString() 적용 필수
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Item")
    FString ItemInstanceID;

    // DataAsset ID 혹은 클래스 식별자 (어떤 종류의 아이템인지 명시)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Item")
    FString ItemTemplateID;

    // 실제 월드에 스폰된 액터 포인터 (Dangling 방지를 위해 코드에서 IsValid 체크 필수)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Item")
    AActor* ItemActor;

    // 기본 생성자
    FDroppedItemData() : ItemInstanceID(TEXT("")), ItemTemplateID(TEXT("")), ItemActor(nullptr) {}

    // 편의용 생성자
    FDroppedItemData(const FString& InInstanceID, const FString& InTemplateID, AActor* InActor)
        : ItemInstanceID(InInstanceID), ItemTemplateID(InTemplateID), ItemActor(InActor) {}
};

/**
 * 월드 내 드랍된 아이템의 추적 및 MCP 동기화를 수행하는 전역 서브시스템.
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UItemManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Lifecycle
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // 수동 배치건 동적 스폰이건 스폰 시점에 매니저에 등록하여 추적 풀(Pool)에 진입시킵니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    void RegisterDroppedItem(const FString& InInstanceID, AActor* InItemActor, const FString& InTemplateID);

    // 파괴되거나 인벤토리에 들어갈 때 추적 풀에서 제거해 메모리 누수와 크래시를 막습니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    void UnregisterDroppedItem(const FString& InInstanceID);

    // 인스턴스 ID를 통해 특정 아이템 액터를 O(1)로 빠르게 가져옵니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    AActor* GetItemActorByID(const FString& InInstanceID) const;

    // NPC 주변의 아이템을 찾을 때, 맵(TMap) 전체를 순회하지 않고 언리얼 물리 쿼리(OverlapMulti)를 수행하여 극한의 성능 최적화를 달성합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    TArray<FDroppedItemData> GetItemsInRange(const FVector& SearchLocation, float SearchRadius) const;

    // 네트워크 병목 예방을 위해 모든 상태를 매 프레임 동기화하는 대신, 필요 시점에 일괄 전송 가능한 포맷으로 조립합니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    FString SerializeActiveItemsToJson() const;

    // DataTable에서 ItemID(TemplateID)에 해당하는 아이템 원본 데이터를 가져옵니다. (레지스트리 및 하드코딩 에셋 로딩 방식 대체)
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    bool GetItemDataByID(const FString& InTemplateID, FItemData& OutItemData) const;

    // 아이템 원본 데이터를 정의해 둔 마스터 데이터 테이블. 언리얼 에디터(BP)에서 반드시 세팅해야 합니다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Item")
    class UDataTable* GlobalItemDataTable;

private:
    // 활성화된 아이템들을 인스턴스 ID(Key)로 즉시 찾을 수 있도록 캐싱하는 메모리 풀입니다.
    UPROPERTY()
    TMap<FString, FDroppedItemData> ActiveDroppedItems;
};
