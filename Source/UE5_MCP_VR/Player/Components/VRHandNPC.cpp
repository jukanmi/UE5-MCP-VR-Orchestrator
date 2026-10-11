#include "Player/Components/VRHandComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"

#include "Core/BP/CombatCharacter.h"
#include "Core/Physics/CapsuleContact.h"
#include "Core/Physics/NPCBoneCapsuleSet.h"
#include "Player/BP/VRPawnAnimInstance.h"

namespace
{
    using FHandSegments = TArray<FContactSegment, TInlineAllocator<2 * UVRPawnAnimInstance::NumPalmShapes + UVRPawnAnimInstance::NumFingerShapes>>;

    FHandSegments BuildHandSegments(const TArray<TObjectPtr<UBoxComponent>>& PalmShapes,
                                    const TArray<TObjectPtr<UCapsuleComponent>>& FingerBodies, bool bFingertipsActive)
    {
        // 손 모양 = 손바닥 바디 기준 선분 + 반지름. 손바닥 상자마다 긴 축을 따라 짧은 축 양 끝에 캡슐 두 줄(반지름 = 두께 절반),
        // 손가락은 켜져 있을 때(핸드트래킹)만 캡슐 그대로.
        FHandSegments Segments;
        for (const UBoxComponent* Shape : PalmShapes)
        {
            if (!Shape) continue;
            const FTransform ShapeRel = Shape->GetRelativeTransform();
            const FVector Extent = Shape->GetUnscaledBoxExtent();
            const float HalfThickness = Extent.Z;
            const FVector LongAxis = Extent.X >= Extent.Y ? FVector(FMath::Max(Extent.X - HalfThickness, 0.f), 0.f, 0.f) : FVector(0.f, FMath::Max(Extent.Y - HalfThickness, 0.f), 0.f);
            const FVector ShortAxis = Extent.X >= Extent.Y ? FVector(0.f, FMath::Max(Extent.Y - HalfThickness, 0.f), 0.f) : FVector(FMath::Max(Extent.X - HalfThickness, 0.f), 0.f, 0.f);
            Segments.Add({ ShapeRel.TransformPosition(ShortAxis - LongAxis), ShapeRel.TransformPosition(ShortAxis + LongAxis), HalfThickness });
            Segments.Add({ ShapeRel.TransformPosition(-ShortAxis - LongAxis), ShapeRel.TransformPosition(-ShortAxis + LongAxis), HalfThickness });
        }
        if (bFingertipsActive)
        {
            for (const UCapsuleComponent* Finger : FingerBodies)
            {
                if (!Finger) continue;
                const FTransform ShapeRel = Finger->GetRelativeTransform();
                const FVector HalfAxis(0.f, 0.f, Finger->GetUnscaledCapsuleHalfHeight_WithoutHemisphere());
                Segments.Add({ ShapeRel.TransformPosition(-HalfAxis), ShapeRel.TransformPosition(HalfAxis), Finger->GetUnscaledCapsuleRadius() });
            }
        }
        return Segments;
    }
}

// ============================================================================
// NPC — 손 드라이브 목표를 NPC 뼈 캡슐 밖으로 투영, 닿은 NPC 밀기
// ============================================================================

void UVRHandComponent::PushTouchedNPC(ACombatCharacter* NPC, const FVector& Surface, const FVector& RealHand, float DeltaTime)
{
    // 실제 손이 NPC 뼈 캡슐 안으로 들어간 깊이(표면으로 옮긴 목표 ↔ 실제 손)에 비례한 속도로, 들어간 방향(수평)으로 민다.
    // 물리 손이 그 표면에 와 있을 때만 — 벽에 막혀 NPC 에 못 닿았으면 실제 손이 NPC 안에 있어도 밀지 않는다.
    // 스윙(UVRMeleeComponent::TryMeleeHits)은 때리기 임계 이상 순간 타격이고, 이건 느리게 대고 미는 접촉 몫이다.
    static constexpr float SurfaceTolerance = 2.f;   // 물리 손 ↔ 표면 목표(cm). 빈손 드라이브는 목표를 1cm 안으로 따른다.
    if (!NPC || NPC->bIsDead || PushGain <= 0.f) return;
    if (FVector::DistSquared(Palm->GetComponentLocation(), Surface) > FMath::Square(SurfaceTolerance)) return;

    // 스윕으로 옮겨 벽·지형에 막히면 거기서 멈춘다.
    const FVector PushVel = CapsuleContact::PushVelocity(Surface, RealHand, PushMinError, PushGain, MaxPushSpeed);
    if (!PushVel.IsZero()) NPC->AddActorWorldOffset(PushVel * DeltaTime, /*bSweep=*/true);
}

FVector UVRHandComponent::ProjectOutOfNPCs(const FVector& Location, const FQuat& Rotation, ACombatCharacter*& OutTouched) const
{
    // 손 모양이 닿을 수 있는 NPC 의 뼈 캡슐만 모은다.
    static constexpr float HandReach = 30.f;   // 손바닥 바디 중심 → 손 모양 끝(손끝)까지 여유(cm)
    OutTouched = nullptr;
    TArray<FNPCWorldCapsule> Bones;
    TArray<ACombatCharacter*> BoneOwners;   // Bones 와 같은 순서
    CapsuleContact::GatherBoneCapsules(GetWorld(), Location, HandReach, Bones, BoneOwners);
    if (Bones.IsEmpty()) return Location;

    // 밀어낼 방향 기준은 지금 물리 손바닥 — 실제 손이 뼈 축을 넘어 들어가도(몸통 반지름 ≈ 15cm) 반대편 표면으로 튀지 않는다.
    const FHandSegments Segments = BuildHandSegments(PalmShapes, FingerBodies, bFingertipsActive);
    int32 DeepestBone = INDEX_NONE;
    const FVector Target = CapsuleContact::ProjectOut(Segments, Location, Rotation, Palm->GetComponentTransform(), Bones, DeepestBone);
    if (BoneOwners.IsValidIndex(DeepestBone)) OutTouched = BoneOwners[DeepestBone];   // 실제 손 위치에서 가장 깊이 들어간 NPC
    return Target;
}
