#pragma once

#include "CoreMinimal.h"

/**
 * 정점 구름에 단순 충돌 도형을 맞추는 공용 코어 — NPC 뼈 캡슐과 아이템 충돌이 같이 쓴다.
 * 범위와 반지름을 최대값이 아닌 백분위로 잡아, 튀는 정점(장식·이펙트 조각)에 도형이 끌려가지 않게 한다.
 */
namespace CollisionFit
{
    /** V 를 정렬한 뒤 P(0~1) 백분위 값. V 는 비어 있으면 안 된다. */
    float Percentile(TArray<float>& V, float P);

    /** 축 위 캡슐 — Origin 에서 축 방향 거리(cm)로 잡은 선분 구간과 반지름. Start == End 면 구. */
    struct FAxisCapsule
    {
        float Start = 0.f;
        float End = 0.f;
        float Radius = 0.f;
    };

    /**
     * 주어진 축(Origin + Dir, Dir 은 단위 벡터)에 캡슐을 맞춘다.
     * 반지름 = 축까지 거리의 RadiusPercentile 백분위, 축 방향 범위 = ExtentPercentile ~ 1-ExtentPercentile 백분위.
     * 양 끝 반구가 범위 끝에 닿게 선분을 반지름만큼 안으로 줄이고, 너무 짧으면 범위 가운데의 구가 된다.
     */
    FAxisCapsule FitCapsuleOnAxis(TConstArrayView<FVector> Points, const FVector& Origin, const FVector& Dir, float RadiusPercentile, float ExtentPercentile);
}
