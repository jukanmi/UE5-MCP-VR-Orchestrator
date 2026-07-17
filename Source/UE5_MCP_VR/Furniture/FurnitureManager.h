#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FurnitureManager.generated.h"

class AFurnitureActor;

/**
 * 월드 가구 등록소 (GameInstanceSubsystem — ItemManager 패턴).
 *
 * FurnitureID → 액터 조회 단일 소스. 등록/해제는 AFurnitureActor 의 BeginPlay/EndPlay 가 수행.
 * 소비처: ResolveActionTarget(가구 ID 타겟 해석) · NPCManager(valid_targets 조립).
 */
UCLASS()
class UE5_MCP_VR_API UFurnitureManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** 월드 컨텍스트에서 서브시스템 획득 — UNPCManager::Get 과 동일 패턴. */
    static UFurnitureManager* Get(const UObject* WorldContext);

    UFUNCTION(BlueprintCallable, Category = "MCP|Furniture")
    void RegisterFurniture(const FString& InFurnitureID, AFurnitureActor* InFurniture);

    UFUNCTION(BlueprintCallable, Category = "MCP|Furniture")
    void UnregisterFurniture(const FString& InFurnitureID);

    UFUNCTION(BlueprintCallable, Category = "MCP|Furniture")
    AFurnitureActor* GetFurnitureByID(const FString& InFurnitureID) const;

    /** 반경 내 최근접 빈 착석 가구(Seat/Bed 만 — ReadSpot/PraySpot 제외).
     *  플레이어 Interact 착석 공용(VRPawn·VRPlayerCharacter). 수평 거리 기준. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Furniture")
    AFurnitureActor* FindNearestVacantSitable(const FVector& Location, float Range) const;

    /** valid_targets 조립용 순회 — NPCMap::GetActiveNPCs 와 동일 사용 패턴. */
    const TMap<FString, AFurnitureActor*>& GetActiveFurniture() const { return ActiveFurniture; }

private:
    UPROPERTY()
    TMap<FString, AFurnitureActor*> ActiveFurniture;
};
