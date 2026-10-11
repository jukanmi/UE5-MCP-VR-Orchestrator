#include "Core/Physics/CapsuleContact.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"

#include "Core/BP/CombatCharacter.h"
#include "Core/Physics/NPCBoneCapsuleSet.h"

void CapsuleContact::AppendSegments(TArrayView<const TObjectPtr<UBoxComponent>> Boxes, TArrayView<const TObjectPtr<UCapsuleComponent>> Capsules,
                                    TArray<FContactSegment>& OutSegments)
{
    for (const UBoxComponent* Shape : Boxes)
    {
        if (!Shape) continue;
        const FTransform ShapeRel = Shape->GetRelativeTransform();
        const FVector Extent = Shape->GetUnscaledBoxExtent();
        const float HalfThickness = Extent.Z;
        const FVector LongAxis = Extent.X >= Extent.Y ? FVector(FMath::Max(Extent.X - HalfThickness, 0.f), 0.f, 0.f) : FVector(0.f, FMath::Max(Extent.Y - HalfThickness, 0.f), 0.f);
        const FVector ShortAxis = Extent.X >= Extent.Y ? FVector(0.f, FMath::Max(Extent.Y - HalfThickness, 0.f), 0.f) : FVector(FMath::Max(Extent.X - HalfThickness, 0.f), 0.f, 0.f);
        OutSegments.Add({ ShapeRel.TransformPosition(ShortAxis - LongAxis), ShapeRel.TransformPosition(ShortAxis + LongAxis), HalfThickness });
        OutSegments.Add({ ShapeRel.TransformPosition(-ShortAxis - LongAxis), ShapeRel.TransformPosition(-ShortAxis + LongAxis), HalfThickness });
    }
    for (const UCapsuleComponent* Shape : Capsules)
    {
        if (!Shape) continue;
        const FTransform ShapeRel = Shape->GetRelativeTransform();
        const FVector HalfAxis(0.f, 0.f, Shape->GetUnscaledCapsuleHalfHeight_WithoutHemisphere());
        OutSegments.Add({ ShapeRel.TransformPosition(-HalfAxis), ShapeRel.TransformPosition(HalfAxis), Shape->GetUnscaledCapsuleRadius() });
    }
}

void CapsuleContact::GatherBoneCapsules(UWorld* World, const FVector& Location, float Reach,
                                        TArray<FNPCWorldCapsule>& OutBones, TArray<ACombatCharacter*>& OutOwners)
{
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CapsuleContact] GatherBoneCapsules — World 없음, 접촉 판정 생략"));
        return;
    }

    // 메시 경계는 포즈(애니메이션·래그돌)를 따라간다 — 근처 NPC 만 뼈 캡슐을 월드로 풀어 비용을 막는다.
    TArray<FNPCWorldCapsule> NPCCapsules;
    for (TActorIterator<ACombatCharacter> It(World); It; ++It)
    {
        const USkeletalMeshComponent* NPCMesh = It->GetMesh();
        if (!It->BoneCapsules || !NPCMesh) continue;
        if (FVector::DistSquared(NPCMesh->Bounds.Origin, Location) > FMath::Square(NPCMesh->Bounds.SphereRadius + Reach)) continue;
        if (!It->BoneCapsules->GetWorldCapsules(NPCMesh, NPCCapsules)) continue;
        OutBones.Append(NPCCapsules);
        while (OutOwners.Num() < OutBones.Num()) OutOwners.Add(*It);
    }
}

FVector CapsuleContact::ProjectOut(TArrayView<const FContactSegment> LocalSegments, const FVector& Location, const FQuat& Rotation,
                                   const FTransform& ReferenceTM, TArrayView<const FNPCWorldCapsule> Bones, int32& OutDeepestBone)
{
    // 가장 깊이 박힌 짝의 깊이만큼 목표를 밀어내고 되풀이한다.
    // 축을 넘은 목표는 축 기준으로 들어온 쪽에 비춰서 본다. 그대로 두면 목표가 표면을 따라 축 너머 쪽으로 계속 끌려
    // 형상이 몸을 돌아 뒤로 미끄러진다(헤드셋 없는 PIE 실측: 몸통 축 10cm 너머에서 손이 앞 → 뒤 표면으로 돌아감).
    // 목표가 반대편 표면 밖까지 나가면 더 겹치지 않아 형상이 몸을 지나간다.
    OutDeepestBone = INDEX_NONE;
    FVector Target = Location;
    for (int32 Iter = 0; Iter < MaxDepenetrationIters; ++Iter)
    {
        const FTransform TargetTM(Rotation, Target);
        float MaxOverlap = 0.f;
        FVector PushOut = FVector::ZeroVector;
        for (const FContactSegment& Seg : LocalSegments)
        {
            const FVector SegStart = TargetTM.TransformPosition(Seg.Start);
            const FVector SegEnd = TargetTM.TransformPosition(Seg.End);
            for (int32 BoneIdx = 0; BoneIdx < Bones.Num(); ++BoneIdx)
            {
                const FNPCWorldCapsule& Bone = Bones[BoneIdx];
                FVector OnSeg, OnBone;
                FMath::SegmentDistToSegmentSafe(SegStart, SegEnd, Bone.A, Bone.B, OnSeg, OnBone);
                const float TouchDist = Seg.Radius + Bone.Radius;
                if (FVector::DistSquared(OnSeg, OnBone) >= FMath::Square(TouchDist)) continue;

                FVector RefOnSeg, RefOnBone;
                FMath::SegmentDistToSegmentSafe(ReferenceTM.TransformPosition(Seg.Start), ReferenceTM.TransformPosition(Seg.End),
                                                Bone.A, Bone.B, RefOnSeg, RefOnBone);
                FVector PushDir = (RefOnSeg - RefOnBone).GetSafeNormal();
                if (PushDir.IsZero()) PushDir = (OnSeg - OnBone).GetSafeNormal();
                if (PushDir.IsZero()) continue;   // 기준·목표 둘 다 뼈 축 위 — 방향을 모른다

                const FVector BoneToSeg = OnSeg - OnBone;
                const bool bCrossed = FVector::DotProduct(BoneToSeg, PushDir) < 0.f;
                const float Overlap = TouchDist - FMath::Abs(FVector::DotProduct(BoneToSeg, PushDir));
                if (Overlap > MaxOverlap)
                {
                    MaxOverlap = Overlap;
                    PushOut = (bCrossed ? -2.f * BoneToSeg : FVector::ZeroVector) + PushDir * Overlap;
                    if (Iter == 0) OutDeepestBone = BoneIdx;
                }
            }
        }
        if (MaxOverlap <= KINDA_SMALL_NUMBER) break;
        Target += PushOut;
    }
    return Target;
}

FVector CapsuleContact::PushVelocity(const FVector& Surface, const FVector& Penetrator, float MinDepth, float Gain, float MaxSpeed)
{
    const float Depth = FVector::Dist(Penetrator, Surface);
    FVector Dir = Penetrator - Surface;
    Dir.Z = 0.f;
    if (Depth < MinDepth || !Dir.Normalize()) return FVector::ZeroVector;
    return Dir * FMath::Min((Depth - MinDepth) * Gain, MaxSpeed);
}
