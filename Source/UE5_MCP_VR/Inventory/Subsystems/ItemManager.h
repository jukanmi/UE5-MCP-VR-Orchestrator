#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ItemManager.generated.h"

class ADroppedItemBase;
class UDataTable;

// 월드 상에 드랍된 아이템의 식별 정보와 실제 포인터를 묶어 MCP(Python)로의 상태 동기화 및 쿼리를 용이하게 합니다.
USTRUCT(BlueprintType)
struct FDroppedItemData
{
    GENERATED_BODY()

    // 고유 식별자 (UUID). FGuid::NewGuid().ToString() 적용 필수
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Item")
    FString ItemInstanceID;

    // DataAsset ID 혹은 클래스 식별자 (어떤 종류의 아이템인지 명시)
    // EditAnywhere — 월드에 직접 배치한 드랍 아이템의 종류를 에디터에서 지정해야 픽업 시 DataTable 조회가 된다.
    // (InstanceID/ItemActor 는 BeginPlay 가 발급하므로 계속 읽기 전용)
    // GetOptions — DT_ItemRegistry 행 목록 드롭다운. 오타 ID 는 픽업 시 조회 실패로만 드러난다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Item",
              meta = (GetOptions = "/Script/UE5_MCP_VR.ItemRegistryOptions.GetItemIDOptions"))
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
    // 서브시스템 획득 단축 — GameInstance 를 거쳐 꺼내는 3줄을 호출처마다 반복하지 않는다(NPCManager 와 같은 방식).
    static UItemManager* Get(const UObject* WorldContext);

    // Lifecycle
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // 수동 배치건 동적 스폰이건 스폰 시점에 매니저에 등록하여 추적 풀(Pool)에 진입시킵니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    void RegisterDroppedItem(const FString& InInstanceID, AActor* InItemActor, const FString& InTemplateID);

    // 파괴되거나 인벤토리에 들어갈 때 추적 풀에서 제거해 메모리 누수와 크래시를 막습니다.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    void UnregisterDroppedItem(const FString& InInstanceID);

    // 반경 내 등록된 드랍 아이템 — TMap 전체 순회 대신 물리 오버랩(PhysicsBody/WorldDynamic)으로 후보를 추린다.
    // 반환은 액터 자체: 호출처(플레이어 픽업·손 쥐기·NPC 픽업)가 전부 ADroppedItemBase 로 캐스팅해 쓰기 때문.
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    TArray<ADroppedItemBase*> GetItemsInRange(const FVector& SearchLocation, float SearchRadius) const;

    // DataTable에서 ItemID(TemplateID)에 해당하는 아이템 원본 데이터를 가져옵니다. (레지스트리 및 하드코딩 에셋 로딩 방식 대체)
    UFUNCTION(BlueprintCallable, Category = "MCP|Item")
    bool GetItemDataByID(const FString& InTemplateID, FItemData& OutItemData) const;

private:
    // 아이템 마스터 테이블(DT_ItemRegistry). GameInstanceSubsystem 은 에디터 Details 가 없어 EditAnywhere 로 둬도
    // 설정할 수 없으므로(항상 null → 매 호출 StaticLoadObject 폴백이던 성능 문제), Initialize 에서 컨벤션 경로로 1회 로드.
    UPROPERTY(Transient)
    TObjectPtr<UDataTable> ItemTable;

    // 활성화된 아이템들을 인스턴스 ID(Key)로 즉시 찾을 수 있도록 캐싱하는 메모리 풀입니다.
    UPROPERTY()
    TMap<FString, FDroppedItemData> ActiveDroppedItems;
};
