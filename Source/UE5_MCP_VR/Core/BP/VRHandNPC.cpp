#include "Core/BP/VRHandComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/BP/CombatCharacter.h"
#include "Core/BP/VRPawnAnimInstance.h"
#include "Core/Physics/NPCBoneCapsuleSet.h"
#include "EngineUtils.h"

// ============================================================================
// NPC — 손 드라이브 목표를 NPC 뼈 캡슐 밖으로 투영, 닿은 NPC 밀기
// ============================================================================

void UVRHandComponent::PushTouchedNPC(ACombatCharacter* NPC, const FVector& Surface, const FVector& RealHand, float DeltaTime)
{
    // 실제 손이 NPC 뼈 캡슐 안으로 들어간 깊이(표면으로 옮긴 목표 ↔ 실제 손)에 비례한 속도로, 들어간 방향(수평)으로 민다.
    // 물리 손이 그 표면에 와 있을 때만 — 벽에 막혀 NPC 에 못 닿았으면 실제 손이 NPC 안에 있어도 밀지 않는다.
    // 스윙(폰의 TryMeleeHits)은 1m/s 이상 순간 타격이고, 이건 느리게 대고 미는 접촉 몫이다.
    static constexpr float SurfaceTolerance = 2.f;   // 물리 손 ↔ 표면 목표(cm). 빈손 드라이브는 목표를 1cm 안으로 따른다.
    if (!NPC || NPC->bIsDead || PushGain <= 0.f) return;
    if (FVector::DistSquared(Palm->GetComponentLocation(), Surface) > FMath::Square(SurfaceTolerance)) return;

    const float Depth = FVector::Dist(RealHand, Surface);
    FVector Dir = RealHand - Surface;
    Dir.Z = 0.f;
    if (Depth < PushMinError || !Dir.Normalize()) return;

    // 스윕으로 옮겨 벽·지형에 막히면 거기서 멈춘다.
    const float PushSpeed = FMath::Min((Depth - PushMinError) * PushGain, MaxPushSpeed);
    NPC->AddActorWorldOffset(Dir * PushSpeed * DeltaTime, /*bSweep=*/true);
}

FVector UVRHandComponent::ProjectOutOfNPCs(const FVector& Location, const FQuat& Rotation, ACombatCharacter*& OutTouched) const
{
    // 손 모양이 닿을 수 있는 NPC 의 뼈 캡슐만 모은다. 메시 경계는 포즈(애니메이션·래그돌)를 따라간다.
    // 뼈 캡슐 치수가 없는 NPC 는 GetWorldCapsules 가 거절하고, 그 NPC 는 몸 캡슐이 손을 막는다.
    static constexpr float HandReach = 30.f;   // 손바닥 바디 중심 → 손 모양 끝(손끝)까지 여유(cm)
    OutTouched = nullptr;
    TArray<FNPCWorldCapsule> Bones, Caps;
    TArray<ACombatCharacter*> BoneOwners;   // Bones 와 같은 순서
    for (TActorIterator<ACombatCharacter> It(GetWorld()); It; ++It)
    {
        const USkeletalMeshComponent* NPCMesh = It->GetMesh();
        if (!It->BoneCapsules || !NPCMesh) continue;
        if (FVector::DistSquared(NPCMesh->Bounds.Origin, Location) > FMath::Square(NPCMesh->Bounds.SphereRadius + HandReach)) continue;
        if (!It->BoneCapsules->GetWorldCapsules(NPCMesh, Caps)) continue;
        Bones.Append(Caps);
        while (BoneOwners.Num() < Bones.Num()) BoneOwners.Add(*It);
    }
    if (Bones.IsEmpty()) return Location;

    // 손 모양 = 손바닥 바디 기준 선분 + 반지름. 손바닥 상자마다 긴 축을 따라 짧은 축 양 끝에 캡슐 두 줄(반지름 = 두께 절반),
    // 손가락은 켜져 있을 때(핸드트래킹)만 캡슐 그대로.
    struct FHandSegment { FVector A, B; float Radius; };
    TArray<FHandSegment, TInlineAllocator<2 * UVRPawnAnimInstance::NumPalmShapes + UVRPawnAnimInstance::NumFingerShapes>> Segments;
    for (const UBoxComponent* Shape : PalmShapes)
    {
        if (!Shape) continue;
        const FTransform Rel = Shape->GetRelativeTransform();
        const FVector E = Shape->GetUnscaledBoxExtent();
        const float R = E.Z;
        const FVector Long = E.X >= E.Y ? FVector(FMath::Max(E.X - R, 0.f), 0.f, 0.f) : FVector(0.f, FMath::Max(E.Y - R, 0.f), 0.f);
        const FVector Side = E.X >= E.Y ? FVector(0.f, FMath::Max(E.Y - R, 0.f), 0.f) : FVector(FMath::Max(E.X - R, 0.f), 0.f, 0.f);
        Segments.Add({ Rel.TransformPosition(Side - Long), Rel.TransformPosition(Side + Long), R });
        Segments.Add({ Rel.TransformPosition(-Side - Long), Rel.TransformPosition(-Side + Long), R });
    }
    if (bFingertipsActive)
    {
        for (const UCapsuleComponent* Finger : FingerBodies)
        {
            if (!Finger) continue;
            const FTransform Rel = Finger->GetRelativeTransform();
            const FVector Half(0.f, 0.f, Finger->GetUnscaledCapsuleHalfHeight_WithoutHemisphere());
            Segments.Add({ Rel.TransformPosition(-Half), Rel.TransformPosition(Half), Finger->GetUnscaledCapsuleRadius() });
        }
    }

    // 가장 깊이 박힌 짝의 깊이만큼 목표를 밀어내고, 밀린 곳에서 다른 캡슐에 박혔을 수 있으니 몇 번 되풀이한다. 위치만 옮기고 회전은 그대로.
    // 밀어낼 방향은 지금 물리 손 쪽 — 실제 손이 뼈 축을 넘어 들어가도(몸통 반지름 ≈ 15cm) 반대편 표면으로 튀지 않는다.
    // 축을 넘은 실제 손은 축 기준으로 들어온 쪽에 비춰서 본다. 그대로 두면 목표가 표면을 따라 축 너머 쪽으로 계속 끌려
    // 손이 몸을 돌아 뒤로 미끄러진다(헤드셋 없는 PIE 실측: 몸통 축 10cm 너머에서 손이 앞 → 뒤 표면으로 돌아감).
    // 실제 손이 반대편 표면 밖까지 나가면 더 겹치지 않아 물리 손이 몸을 지나간다.
    const FTransform BodyTM = Palm->GetComponentTransform();
    FVector Target = Location;
    for (int32 Iter = 0; Iter < 4; ++Iter)
    {
        const FTransform TargetTM(Rotation, Target);
        float Depth = 0.f;
        FVector Push = FVector::ZeroVector;
        for (const FHandSegment& S : Segments)
        {
            const FVector A = TargetTM.TransformPosition(S.A);
            const FVector B = TargetTM.TransformPosition(S.B);
            for (int32 b = 0; b < Bones.Num(); ++b)
            {
                const FNPCWorldCapsule& C = Bones[b];
                FVector P, Q;
                FMath::SegmentDistToSegmentSafe(A, B, C.A, C.B, P, Q);
                const float Reach = S.Radius + C.Radius;
                if (FVector::DistSquared(P, Q) >= FMath::Square(Reach)) continue;

                FVector BodyP, BodyQ;
                FMath::SegmentDistToSegmentSafe(BodyTM.TransformPosition(S.A), BodyTM.TransformPosition(S.B), C.A, C.B, BodyP, BodyQ);
                FVector N = (BodyP - BodyQ).GetSafeNormal();
                if (N.IsZero()) N = (P - Q).GetSafeNormal();
                if (N.IsZero()) continue;   // 물리 손·목표 둘 다 뼈 축 위 — 방향을 모른다
                const FVector Gap = P - Q;
                const bool bCrossed = FVector::DotProduct(Gap, N) < 0.f;
                const float D = Reach - FMath::Abs(FVector::DotProduct(Gap, N));
                if (D > Depth)
                {
                    Depth = D;
                    Push = (bCrossed ? -2.f * Gap : FVector::ZeroVector) + N * D;
                    if (Iter == 0) OutTouched = BoneOwners[b];   // 실제 손 위치에서 가장 깊이 들어간 NPC
                }
            }
        }
        if (Depth <= KINDA_SMALL_NUMBER) break;
        Target += Push;
    }
    return Target;
}
