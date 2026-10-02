#include "Core/Physics/CollisionFit.h"

namespace CollisionFit
{
    float Percentile(TArray<float>& V, float P)
    {
        V.Sort();
        return V[FMath::Clamp(FMath::RoundToInt(P * (V.Num() - 1)), 0, V.Num() - 1)];
    }

    FAxisCapsule FitCapsuleOnAxis(TConstArrayView<FVector> Points, const FVector& Origin, const FVector& Dir, float RadiusPercentile, float ExtentPercentile)
    {
        // 축 방향 거리(T)와 축까지 거리(D).
        TArray<float> T, D;
        T.Reserve(Points.Num());
        D.Reserve(Points.Num());
        for (const FVector& P : Points)
        {
            const float t = FVector::DotProduct(P - Origin, Dir);
            T.Add(t);
            D.Add(FVector::Dist(P, Origin + Dir * t));
        }

        FAxisCapsule Out;
        Out.Radius = Percentile(D, RadiusPercentile);
        const float Lo = Percentile(T, ExtentPercentile);
        const float Hi = Percentile(T, 1.f - ExtentPercentile);
        Out.Start = Lo + Out.Radius;
        Out.End = Hi - Out.Radius;
        if (Out.Start > Out.End) { Out.Start = Out.End = 0.5f * (Lo + Hi); }
        return Out;
    }
}
