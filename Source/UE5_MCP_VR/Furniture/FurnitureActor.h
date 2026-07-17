#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FurnitureActor.generated.h"

class USceneComponent;

/** 가구 종류 — Sit→Seat, Sleep→Bed, Read→ReadSpot, Pray→PraySpot 을 기대
 *  (불일치여도 강제하지 않음, LLM 어휘 안내용). 플레이어 Interact 착석은 Seat/Bed 만. */
UENUM(BlueprintType)
enum class EFurnitureType : uint8
{
    Seat     UMETA(DisplayName = "Seat"),
    Bed      UMETA(DisplayName = "Bed"),
    ReadSpot UMETA(DisplayName = "ReadSpot"),
    PraySpot UMETA(DisplayName = "PraySpot"),
};

/**
 * NPC Sit/Sleep 대상 가구 액터 (Phase 1 — SPEC 가구 시스템).
 *
 * BeginPlay 에서 FurnitureManager 에 자기 등록, EndPlay 에서 해제(DroppedItemBase 패턴).
 * 메시는 BP 파생에서 RootComponent 하위에 추가하고 SeatPoint 를 그에 맞춰 정렬한다.
 * 좌표 변환은 GetSeatTransform() 하나로 일원화(§9 — BlueprintPure C++ getter).
 */
UCLASS(Blueprintable, BlueprintType)
class UE5_MCP_VR_API AFurnitureActor : public AActor
{
    GENERATED_BODY()

public:
    AFurnitureActor();

    /** LLM valid_targets 로 노출되는 ID. 빈 값이면 BeginPlay 에서 GUID 자동 발급 —
     *  가독성을 위해 "Chair_Tavern_01" 처럼 의미 있는 값을 에디터에서 직접 지정 권장. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MCP|Furniture")
    FString FurnitureID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MCP|Furniture")
    EFurnitureType FurnitureType = EFurnitureType::Seat;

    /** 착석 스냅 기준점 — BP 파생에서 메시의 앉는 위치·바라볼 방향에 맞춰 정렬. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Furniture")
    USceneComponent* SeatPoint;

    UFUNCTION(BlueprintPure, Category = "MCP|Furniture")
    FTransform GetSeatTransform() const;

    /** 점유 시도. 이미 타인이 점유 중이면 false(호출부는 제자리 착석 폴백).
     *  자기 자신 재점유는 idempotent 허용(재스냅만 반복, 부작용 없음). */
    UFUNCTION(BlueprintCallable, Category = "MCP|Furniture")
    bool TryOccupy(AActor* Occupant);

    /** 실제 점유자만 해제 가능 — 스테일 Release 가 타 점유자를 쫓아내는 것 방지. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Furniture")
    void Release(AActor* Occupant);

    UFUNCTION(BlueprintPure, Category = "MCP|Furniture")
    bool IsOccupied() const { return OccupantWeak.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "MCP|Furniture")
    AActor* GetOccupant() const { return OccupantWeak.Get(); }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** 디버그 노출용 파생 미러 — 소스 오브 트루스는 OccupantWeak.
     *  점유자 파괴 시 약참조가 자동 invalid 되어 스테일 점유를 스스로 회복한다. */
    UPROPERTY(VisibleAnywhere, Category = "MCP|Furniture", meta = (AllowPrivateAccess = "true"))
    bool bOccupied = false;

    TWeakObjectPtr<AActor> OccupantWeak;
};
