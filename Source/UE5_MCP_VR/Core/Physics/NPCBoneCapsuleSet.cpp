#include "Core/Physics/NPCBoneCapsuleSet.h"

#include "Components/SkeletalMeshComponent.h"
#include "Core/Physics/CollisionFit.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"

#if WITH_EDITOR
#include "MeshUtilitiesCommon.h"
#include "MeshUtilitiesEngine.h"
#endif

const FNPCMeshCapsuleFits* UNPCBoneCapsuleSet::FindFits(const USkeletalMesh* Mesh) const
{
    const FNPCMeshCapsuleFits* Row = Meshes.FindByPredicate([Mesh](const FNPCMeshCapsuleFits& R) { return R.Mesh == Mesh; });
    return Row && Row->Fits.Num() == Capsules.Num() ? Row : nullptr;
}

void UNPCBoneCapsuleSet::FillFromMeshes()
{
#if WITH_EDITOR
    if (Capsules.Num() == 0) return;
    Modify();

    for (FNPCMeshCapsuleFits& Row : Meshes)
    {
        USkeletalMesh* Mesh = Row.Mesh;
        if (!Mesh) continue;
        const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();

        // 기준 포즈의 메시 공간 관절 변환 — 런타임 GetBoneLocation 과 같은 공간(루트 뼈 스케일 포함, cm).
        const TArray<FTransform>& Local = Ref.GetRefBonePose();
        TArray<FTransform> Component;
        Component.SetNum(Local.Num());
        for (int32 i = 0; i < Local.Num(); ++i)
        {
            const int32 Parent = Ref.GetParentIndex(i);
            Component[i] = Parent == INDEX_NONE ? Local[i] : Local[i] * Component[Parent];
        }

        TArray<int32> StartIdx, EndIdx, SkipIdx;
        bool bValid = true;
        for (const FNPCBoneCapsule& C : Capsules)
        {
            StartIdx.Add(Ref.FindBoneIndex(C.StartBone));
            EndIdx.Add(C.EndBone.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(C.EndBone));
            if (StartIdx.Last() == INDEX_NONE || (!C.EndBone.IsNone() && EndIdx.Last() == INDEX_NONE))
            {
                UE_LOG(LogTemp, Warning, TEXT("[BoneCapsule] %s 에 뼈 없음: %s → %s"), *Mesh->GetName(), *C.StartBone.ToString(), *C.EndBone.ToString());
                bValid = false;
            }
        }
        for (const FName& S : SkipBones) { SkipIdx.Add(Ref.FindBoneIndex(S)); }
        if (!bValid) continue;

        // 뼈마다 그 뼈가 가장 많이 잡은 정점. 지정 안 된 뼈의 정점은 가장 가까운 지정 조상 캡슐 몫(손가락 → 손, 목 → 몸통),
        // 조상이 없으면 몸통, Skip 뼈 아래는 버린다.
        TArray<FBoneVertInfo> Infos;
        FMeshUtilitiesEngine::CalcBoneVertInfos(Mesh, Infos, /*bOnlyDominant=*/true);

        TArray<TArray<FVector>> Points;
        Points.SetNum(Capsules.Num());
        for (int32 Bone = 0; Bone < Infos.Num(); ++Bone)
        {
            int32 Owner = 0;
            for (int32 B = Bone; B != INDEX_NONE; B = Ref.GetParentIndex(B))
            {
                if (SkipIdx.Contains(B)) { Owner = INDEX_NONE; break; }
                const int32 k = StartIdx.IndexOfByKey(B);
                if (k != INDEX_NONE) { Owner = k; break; }
            }
            if (Owner == INDEX_NONE) continue;
            for (const FVector3f& P : Infos[Bone].Positions) { Points[Owner].Add(Component[Bone].TransformPosition(FVector(P))); }
        }

        Row.Fits.SetNum(Capsules.Num());
        for (int32 i = 0; i < Capsules.Num(); ++i)
        {
            if (Points[i].Num() == 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("[BoneCapsule] %s %s: 정점 없음, 기존 값 유지"), *Mesh->GetName(), *Capsules[i].StartBone.ToString());
                continue;
            }
            const FVector A = Component[StartIdx[i]].GetLocation();
            FVector Dir;
            if (EndIdx[i] != INDEX_NONE)
            {
                Dir = (Component[EndIdx[i]].GetLocation() - A).GetSafeNormal();
            }
            else
            {
                // 끝 부위 축 = 관절 → 정점 무게중심. 축이 관절과 부위 중심을 함께 지나야 반지름이 안 부푼다
                // (주축 방향만 쓰고 선을 관절에 걸면 손바닥·머리 중심이 축에서 벗어나 구처럼 커진다).
                FVector Mean = FVector::ZeroVector;
                for (const FVector& P : Points[i]) Mean += P;
                Dir = (Mean / Points[i].Num() - A).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
            }
            FNPCCapsuleFit& Fit = Row.Fits[i];
            Fit.Axis = Component[StartIdx[i]].InverseTransformVectorNoScale(Dir).GetSafeNormal();
            const CollisionFit::FAxisCapsule Cap = CollisionFit::FitCapsuleOnAxis(Points[i], A, Dir, RadiusPercentile, ExtentPercentile);
            Fit.Radius = Cap.Radius;
            Fit.Start = Cap.Start;
            Fit.End = Cap.End;
        }
        UE_LOG(LogTemp, Log, TEXT("[BoneCapsule] %s 맞춤 완료 (캡슐 %d)"), *Mesh->GetName(), Row.Fits.Num());
    }
    MarkPackageDirty();
#endif
}

bool UNPCBoneCapsuleSet::GetWorldCapsules(const USkeletalMeshComponent* MeshComp, TArray<FNPCWorldCapsule>& Out) const
{
    const FNPCMeshCapsuleFits* Row = MeshComp ? FindFits(MeshComp->GetSkeletalMeshAsset()) : nullptr;
    if (!Row) return false;

    const float Scale = MeshComp->GetComponentScale().GetAbsMax();
    Out.Reset(Capsules.Num());
    for (int32 i = 0; i < Capsules.Num(); ++i)
    {
        const FNPCBoneCapsule& C = Capsules[i];
        const int32 Bone = MeshComp->GetBoneIndex(C.StartBone);
        if (Bone == INDEX_NONE) continue;
        const FNPCCapsuleFit& Fit = Row->Fits[i];
        const FTransform BoneTM = MeshComp->GetBoneTransform(Bone);
        const FVector A = BoneTM.GetLocation();
        const FVector Dir = BoneTM.TransformVectorNoScale(Fit.Axis).GetSafeNormal();
        Out.Add({ A + Dir * Fit.Start * Scale, A + Dir * Fit.End * Scale, Fit.Radius * Scale, C.StartBone });
    }
    return true;
}

void UNPCBoneCapsuleSet::DrawDebug(const USkeletalMeshComponent* MeshComp, float Duration) const
{
    TArray<FNPCWorldCapsule> Caps;
    if (!MeshComp || !GetWorldCapsules(MeshComp, Caps)) return;
    for (const FNPCWorldCapsule& C : Caps)
    {
        const FVector Axis = C.B - C.A;
        const FQuat Rot = Axis.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Axis).ToQuat();
        DrawDebugCapsule(MeshComp->GetWorld(), 0.5f * (C.A + C.B), 0.5f * Axis.Size() + C.Radius, C.Radius, Rot, FColor::Cyan, false, Duration);
    }
}
