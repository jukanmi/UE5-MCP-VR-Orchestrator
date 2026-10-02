#include "Core/Physics/ItemCollisionGen.h"

#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "Chaos/Convex.h"
#include "Core/Physics/CollisionFit.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "VHACD.h"

// UnrealEd 의 Private 헤더(GeomFitUtils.h)에만 선언돼 있어 직접 선언한다 — 이 메시를 쓰는 컴포넌트의 물리·내비 충돌을 다시 만든다.
UNREALED_API void RefreshCollisionChange(UStaticMesh& StaticMesh);

namespace
{
    // 손으로 다듬어 검증한 충돌 — bForce 없이는 덮어쓰지 않는다. 새로 손본 메시 이름을 여기에 추가.
    // (수동 20헐 WaterBucket 은 2026-10-03 메시 전량 교체로 사라져 지금은 비어 있다.)
    const TArray<FString> ManualCollisionMeshes;

    // V-HACD 헐 부피 합 / 전체 정점 헐 부피 가 이 이상이면 속이 안 빈 물건 — 헐이 여러 개여도 기본도형을 먼저 시도한다
    // (병 목·두루마리 끝은 오목으로 잡히지만 쥐고 던지기엔 캡슐로 충분). 와인컵 0.07·활 0.25·검 0.41 < 0.5 ≤ 약병·두루마리 0.55 (2026-10-02 실측).
    constexpr double MinSolidRatio = 0.5;

    // 기본도형이 메시 로컬 축 범위에서 튀어나오거나 모자란 정도 / 그 축 크기 의 상한 — 넘으면 바닥에서 뜨거나 묻힌다.
    // 부피 오차 상한(0.5)과 같게 두면 TarBucket 캡슐이 위아래로 19% 튀어나왔다(2026-10-03 실측).
    constexpr float MaxExtentError = 0.15f;

    enum class EShape : uint8 { Box, Sphere, Capsule, Hulls };
    const TCHAR* const ShapeNames[] = { TEXT("상자"), TEXT("구"), TEXT("캡슐"), TEXT("헐") };

    // 정점들의 볼록 헐 — 부피·무게중심·관성 주축을 준다.
    template <typename TVec>
    Chaos::FConvex MakeConvex(const TArray<TVec>& Points)
    {
        TArray<Chaos::FConvex::FVec3Type> P;
        P.Reserve(Points.Num());
        for (const TVec& V : Points) { P.Emplace(V.X, V.Y, V.Z); }
        return Chaos::FConvex(P, 0.f);
    }

    // 헐 여러 개의 부피 합과 부피 가중 무게중심(균일 밀도 — Chaos 와 같은 가정).
    struct FVolumeSum
    {
        double Volume = 0.0;
        FVector Moment = FVector::ZeroVector;
        void Add(const FVector& Center, double V)
        {
            Volume += V;
            Moment += Center * V;
        }
        void Add(const Chaos::FConvex& C) { Add(FVector(C.GetCenterOfMass()), C.GetVolume()); }
        FVector Center(const FVector& Fallback) const { return Volume > UE_SMALL_NUMBER ? Moment / Volume : Fallback; }
    };
}
#endif

FString UItemCollisionGen::GenerateItemCollision(UStaticMesh* Mesh, bool bForce, float MaxConcavity, int32 MaxHulls,
    int32 MaxHullVerts, float MaxPrimitiveError, float ExtentPercentile, float RadiusPercentile)
{
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0) return TEXT("메시 없음");
    const FString Name = Mesh->GetName();
    auto Report = [&Name](const FString& Body)
    {
        const FString Line = Name + TEXT(" | ") + Body;
        UE_LOG(LogTemp, Log, TEXT("[ItemCollision] %s"), *Line);
        return Line;
    };
    if (!bForce)
    {
        if (ManualCollisionMeshes.Contains(Name)) return Report(TEXT("건너뜀(수동 충돌)"));
    }

    const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
    const FPositionVertexBuffer& PosBuf = LOD.VertexBuffers.PositionVertexBuffer;
    TArray<FVector3f> Verts;
    Verts.SetNumUninitialized(PosBuf.GetNumVertices());
    for (int32 i = 0; i < Verts.Num(); ++i) { Verts[i] = PosBuf.VertexPosition(i); }
    TArray<uint32> Indices;
    LOD.IndexBuffer.GetCopy(Indices);

    // 엔진 래퍼와 같은 가드 — 너무 작거나 납작하면 V-HACD 가 죽는다.
    const FBox3f Bounds(Verts);
    if (Verts.Num() < 4 || Indices.Num() < 3 || Bounds.GetSize().GetMax() < 1.f || Bounds.GetSize().GetMin() < 0.1f)
    {
        return Report(TEXT("건너뜀(정점 부족·납작)"));
    }

    // 기준: 전체 정점 볼록 헐. 주축(관성 주축)은 상자·캡슐 방향으로 쓴다.
    const Chaos::FConvex Ref = MakeConvex(Verts);
    const double RefVol = Ref.GetVolume();
    const FVector RefCOM(Ref.GetCenterOfMass());
    const FQuat Frame(Ref.GetRotationOfMass());
    if (RefVol <= UE_SMALL_NUMBER) return Report(TEXT("건너뜀(헐 부피 0)"));

    // V-HACD 직접 호출 — 엔진 래퍼(DecomposeMeshToHulls)는 오목도를 0 으로 고정해 볼록한 물건도 헐 상한까지 쪼갠다.
    // 오목도 = |부분 헐 부피 − 부분 부피| / 전체 헐 부피. 이 값 아래로 떨어지면 더 쪼개지 않는다.
    TArray<TArray<FVector3f>> Hulls;
    {
        VHACD::IVHACD* VH = VHACD::CreateVHACD();
        VHACD::IVHACD::Parameters Params;
        Params.m_resolution = 100000;
        Params.m_concavity = MaxConcavity;
        Params.m_maxConvexHulls = FMath::Max(1, MaxHulls);
        Params.m_maxNumVerticesPerCH = FMath::Max(4, MaxHullVerts);
        Params.m_minVolumePerCH = 0.003f;
        Params.m_projectHullVertices = true;
        Params.m_pca = 1;
        Params.m_oclAcceleration = false;
        if (VH->Compute(reinterpret_cast<const float*>(Verts.GetData()), Verts.Num(), Indices.GetData(), Indices.Num() / 3, Params))
        {
            for (uint32 h = 0; h < VH->GetNConvexHulls(); ++h)
            {
                VHACD::IVHACD::ConvexHull Hull;
                VH->GetConvexHull(h, Hull);
                TArray<FVector3f>& Out = Hulls.AddDefaulted_GetRef();
                for (uint32 v = 0; v < Hull.m_nPoints; ++v)
                {
                    Out.Emplace(Hull.m_points[v * 3], Hull.m_points[v * 3 + 1], Hull.m_points[v * 3 + 2]);
                }
            }
        }
        VH->Clean();
        VH->Release();
    }
    if (Hulls.Num() == 0) return Report(TEXT("실패(V-HACD)"));

    // 볼록하거나(헐 1개) 속이 안 빈(헐 부피비 ≥ MinSolidRatio) 물건이면 기본도형 3종을 맞춰 오차를 잰다.
    FVolumeSum HullSum;
    for (const TArray<FVector3f>& H : Hulls) { HullSum.Add(MakeConvex(H)); }
    const double HullRatio = HullSum.Volume / RefVol;

    TArray<FVector> Points;
    Points.Reserve(Verts.Num());
    for (const FVector3f& P : Verts) { Points.Add(FVector(P)); }

    // 축마다 백분위 범위(Frame 좌표, 원점 RefCOM).
    auto PercentileBox = [&](const FQuat& F, FVector& Lo, FVector& Hi)
    {
        TArray<float> Axis[3];
        for (const FVector& P : Points)
        {
            const FVector L = F.UnrotateVector(P - RefCOM);
            for (int32 k = 0; k < 3; ++k) { Axis[k].Add(L[k]); }
        }
        for (int32 k = 0; k < 3; ++k)
        {
            Lo[k] = CollisionFit::Percentile(Axis[k], ExtentPercentile);
            Hi[k] = CollisionFit::Percentile(Axis[k], 1.f - ExtentPercentile);
        }
    };
    // 크기 오차 기준 = 메시 로컬 축 범위. 에셋은 세워서 임포트되니 바닥에 닿는 면이 이 축 위에 있다 —
    // 도형이 이 범위 밖으로 튀어나오면 바닥에서 뜨고, 모자라면 묻힌다.
    FVector LocalLo, LocalHi;
    PercentileBox(FQuat::Identity, LocalLo, LocalHi);
    const FVector LocalSize = (LocalHi - LocalLo).ComponentMax(FVector(0.1));

    struct FFit
    {
        EShape Shape = EShape::Hulls;
        FQuat Rot = FQuat::Identity;   // 상자·캡슐 회전
        FVector Center = FVector::ZeroVector;
        FVector Size = FVector::ZeroVector; // 상자 크기 / 구·캡슐은 X = 반지름, Y = 캡슐 선분 길이
        double Volume = 0.0;
        float Err = TNumericLimits<float>::Max();
    };
    // 도형의 로컬 축 범위(Center ± HalfExt)로 크기 오차, 부피로 부피 오차 — 둘 다 상한 안이면 큰 쪽이 오차.
    auto Score = [&](FFit& Fit, const FVector& HalfExt)
    {
        const float VolErr = FMath::Abs(Fit.Volume - RefVol) / RefVol;
        float ExtErr = 0.f;
        for (int32 a = 0; a < 3; ++a)
        {
            const double Lo = Fit.Center[a] - RefCOM[a] - HalfExt[a], Hi = Fit.Center[a] - RefCOM[a] + HalfExt[a];
            ExtErr = FMath::Max(ExtErr, float(FMath::Max(FMath::Abs(Lo - LocalLo[a]), FMath::Abs(Hi - LocalHi[a])) / LocalSize[a]));
        }
        Fit.Err = VolErr <= MaxPrimitiveError && ExtErr <= MaxExtentError ? FMath::Max(VolErr, ExtErr) : TNumericLimits<float>::Max();
        return FMath::Max(VolErr, ExtErr);
    };

    float Err[3] = { -1.f, -1.f, -1.f }; // 도형별 가장 작은 오차(상한 무관) — 로그용
    FFit Best;
    if (Hulls.Num() == 1 || HullRatio >= MinSolidRatio)
    {
        // 틀 후보: 메시 로컬 축과 관성 주축. 양동이처럼 폭과 높이가 비슷하면 주축이 아무렇게나 기울어 도형이 뜬다.
        for (const FQuat& F : { FQuat::Identity, Frame })
        {
            FVector Lo, Hi;
            PercentileBox(F, Lo, Hi);
            const FVector Size = Hi - Lo;
            const FVector Center = RefCOM + F.RotateVector(0.5 * (Lo + Hi));
            const FMatrix R = FRotationMatrix::Make(F);

            // 상자.
            FFit Box{ EShape::Box, F, Center, Size, Size.X * Size.Y * Size.Z };
            FVector BoxHalf;
            for (int32 a = 0; a < 3; ++a)
            {
                BoxHalf[a] = 0.5 * (FMath::Abs(R.M[0][a]) * Size.X + FMath::Abs(R.M[1][a]) * Size.Y + FMath::Abs(R.M[2][a]) * Size.Z);
            }

            // 구: 상자 중심에서 정점 거리 백분위.
            TArray<float> Dist;
            for (const FVector& P : Points) { Dist.Add(FVector::Dist(P, Center)); }
            const float SphereR = CollisionFit::Percentile(Dist, RadiusPercentile);
            FFit Sphere{ EShape::Sphere, FQuat::Identity, Center, FVector(SphereR, 0, 0), 4.0 / 3.0 * UE_DOUBLE_PI * FMath::Cube(SphereR) };

            // 캡슐: 이 틀에서 가장 긴 축.
            const int32 Long = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : (Size.Y >= Size.Z ? 1 : 2);
            FVector LongAxis = FVector::ZeroVector;
            LongAxis[Long] = 1.0;
            const FVector Dir = F.RotateVector(LongAxis);
            const CollisionFit::FAxisCapsule Cap = CollisionFit::FitCapsuleOnAxis(Points, Center, Dir, RadiusPercentile, ExtentPercentile);
            const double Len = Cap.End - Cap.Start;
            FFit Capsule{ EShape::Capsule, FRotationMatrix::MakeFromZ(Dir).ToQuat(), Center + Dir * 0.5 * (Cap.Start + Cap.End),
                FVector(Cap.Radius, Len, 0), UE_DOUBLE_PI * FMath::Square(Cap.Radius) * Len + 4.0 / 3.0 * UE_DOUBLE_PI * FMath::Cube(Cap.Radius) };

            const float Raw[3] = {
                Score(Box, BoxHalf),
                Score(Sphere, FVector(SphereR)),
                Score(Capsule, Dir.GetAbs() * 0.5 * Len + FVector(Cap.Radius)),
            };
            const FFit* Fits[3] = { &Box, &Sphere, &Capsule };
            for (int32 k = 0; k < 3; ++k)
            {
                Err[k] = Err[k] < 0.f ? Raw[k] : FMath::Min(Err[k], Raw[k]);
                if (Fits[k]->Err < Best.Err) Best = *Fits[k];
            }
        }
    }
    const EShape Shape = Best.Shape;

    // 적용.
    UBodySetup* BS = Mesh->GetBodySetup();
    if (!BS)
    {
        Mesh->CreateBodySetup();
        BS = Mesh->GetBodySetup();
    }

    // 지킬 무게중심 = 지금 충돌의 무게중심 + 지금 보정값(쥐는 느낌이 그대로 남고, 다시 돌려도 처음 위치 유지).
    // 충돌이 없던 메시는 지킬 위치가 없어 새 충돌 무게중심 그대로(보정 0) — 전체 정점 헐 무게중심은 활처럼 휜 물건에서
    // 빈 공간에 떨어진다(활 17cm·망원경 10cm).
    const FKAggregateGeom& Agg = BS->AggGeom;
    FVolumeSum Old;
    for (const FKConvexElem& E : Agg.ConvexElems) { Old.Add(MakeConvex(E.VertexData)); }
    for (const FKBoxElem& E : Agg.BoxElems) { Old.Add(E.Center, E.GetScaledVolume(FVector::OneVector)); }
    for (const FKSphereElem& E : Agg.SphereElems) { Old.Add(E.Center, E.GetScaledVolume(FVector::OneVector)); }
    for (const FKSphylElem& E : Agg.SphylElems) { Old.Add(E.Center, E.GetScaledVolume(FVector::OneVector)); }
    const bool bHadCollision = Old.Volume > UE_SMALL_NUMBER;
    const FVector KeepCOM = Old.Center(RefCOM) + BS->DefaultInstance.COMNudge;
    Mesh->Modify();
    BS->Modify();
    BS->RemoveSimpleCollision();

    FVector NewCOM = RefCOM;
    double NewVol = RefVol;
    switch (Shape)
    {
    case EShape::Box:
    {
        FKBoxElem E(Best.Size.X, Best.Size.Y, Best.Size.Z);
        E.Center = Best.Center;
        E.Rotation = Best.Rot.Rotator();
        BS->AggGeom.BoxElems.Add(E);
        NewCOM = Best.Center;
        NewVol = E.GetScaledVolume(FVector::OneVector);
        break;
    }
    case EShape::Sphere:
    {
        FKSphereElem E(Best.Size.X);
        E.Center = Best.Center;
        BS->AggGeom.SphereElems.Add(E);
        NewCOM = Best.Center;
        NewVol = E.GetScaledVolume(FVector::OneVector);
        break;
    }
    case EShape::Capsule:
    {
        FKSphylElem E(Best.Size.X, Best.Size.Y);
        E.Center = Best.Center;
        E.Rotation = Best.Rot.Rotator();
        BS->AggGeom.SphylElems.Add(E);
        NewCOM = Best.Center;
        NewVol = E.GetScaledVolume(FVector::OneVector);
        break;
    }
    case EShape::Hulls:
    {
        for (const TArray<FVector3f>& H : Hulls)
        {
            FKConvexElem E;
            for (const FVector3f& P : H) { E.VertexData.Add(FVector(P)); }
            E.UpdateElemBox();
            BS->AggGeom.ConvexElems.Add(E);
        }
        NewCOM = HullSum.Center(RefCOM);
        NewVol = HullSum.Volume;
        break;
    }
    }

    BS->DefaultInstance.COMNudge = bHadCollision ? KeepCOM - NewCOM : FVector::ZeroVector;
    BS->InvalidatePhysicsData();
    BS->CreatePhysicsMeshes();
    Mesh->bCustomizedCollision = true; // 재임포트가 충돌을 덮어쓰지 않게
    RefreshCollisionChange(*Mesh);
    Mesh->MarkPackageDirty();

    // 부피비 = 부피 / 전체 정점 헐 부피(V-HACD 헐 합, 실제로 고른 새 충돌) — 1 보다 많이 작으면 속(컵 안쪽·손잡이 사이)이 비었다.
    return Report(FString::Printf(TEXT("%s | 헐 %d (부피비 %.2f) | 새 부피비 %.2f | 오차 상자 %.2f 구 %.2f 캡슐 %.2f | 무게중심 보정 %.1fcm"),
        ShapeNames[static_cast<uint8>(Shape)], Hulls.Num(), HullRatio, NewVol / RefVol, Err[0], Err[1], Err[2], BS->DefaultInstance.COMNudge.Size()));
#else
    return FString();
#endif
}
