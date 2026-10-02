#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ItemCollisionGen.generated.h"

class UStaticMesh;

/**
 * 아이템 StaticMesh 충돌 자동 생성 — 에디터 전용.
 * Python/MCP: unreal.ItemCollisionGen.generate_item_collision(mesh)
 */
UCLASS()
class UItemCollisionGen : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * 메시 단순 충돌을 물리 비용이 가장 적은 모양으로 교체한다.
     * 1) 전체 정점 볼록 헐 하나로 기준 부피·주축을 잡는다.
     * 2) V-HACD 로 오목도 MaxConcavity 까지 쪼갠다. 헐이 1개이거나 헐 부피 합이 전체 정점 헐의 절반 이상(속이 안 빔)이면
     *    상자·구·캡슐을 백분위로 맞추고, 오차(부피 오차와 주축별 크기 오차 중 큰 것)가 가장 작은 것이
     *    MaxPrimitiveError 이하면 그 도형, 아니면 헐 그대로.
     *    MaxConcavity 0.02 = 컵 속이 비는 값(0.05 면 와인컵 속이 찬다, 라인트레이스 실측).
     *    헐이 2개 이상이면 오목한 물건이라 헐 그대로.
     * 3) 무게중심을 바꾸기 전 충돌의 무게중심으로 되돌리는 오프셋을 BodySetup DefaultInstance.COMNudge 에 저장한다
     *    (다시 돌려도 처음 위치 유지, 충돌이 없던 메시는 0. 컴포넌트가 이 값을 자동으로 읽지 않아 ADroppedItemBase 가 BeginPlay 에서 적용).
     * 손으로 다듬은 메시(cpp 의 ManualCollisionMeshes)는 bForce 없이는 건너뛴다. 반환 = 로그 표 한 줄.
     */
    UFUNCTION(BlueprintCallable, Category = "Item Collision")
    static FString GenerateItemCollision(UStaticMesh* Mesh, bool bForce = false, float MaxConcavity = 0.02f, int32 MaxHulls = 16,
        int32 MaxHullVerts = 32, float MaxPrimitiveError = 0.5f, float ExtentPercentile = 0.01f, float RadiusPercentile = 0.95f);
};
