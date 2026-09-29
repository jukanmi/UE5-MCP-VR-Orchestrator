#include "Core/BP/VRPawnAnimInstance.h"
#include "Core/BP/VRPawn.h"
#include "AnimationRuntime.h"
#include "TwoBoneIK.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HeadMountedDisplayTypes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

namespace
{
    const TCHAR* const FingerNames[5] = { TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Pinky") };

    // 손가락별 첫 관절 키포인트. 엄지는 Metacarpal 부터(메시 Thumb1 이 중수골 위치), 나머지는 Proximal 부터
    // (메시에 중수골 본이 없다). 각 손가락 본 3개는 이 키포인트부터 연속 3개에 대응한다.
    const EHandKeypoint FirstKeypoint[5] = {
        EHandKeypoint::ThumbMetacarpal, EHandKeypoint::IndexProximal, EHandKeypoint::MiddleProximal,
        EHandKeypoint::RingProximal, EHandKeypoint::LittleProximal };

    int32 KeyIndex(int32 Finger, int32 Segment)
    {
        return static_cast<int32>(FirstKeypoint[Finger]) + Segment;
    }

    // OpenXR 관절 프레임은 UE 좌표로 X=손끝 방향, Z=손등 방향이다. 레퍼런스 포즈에서 같은 규칙의 프레임을
    // 본→자식 방향과 월드 위(+Z)로 만들고, 그 프레임 기준 본 회전을 보정 값으로 삼는다.
    // ponytail: 손등=+Z 는 레퍼런스 포즈가 손바닥 아래 T포즈라는 가정 — 엄지는 손톱 방향이 달라 비틀릴 수 있다. 틀리면 엄지만 손톱 축을 따로 준다.
    FQuat JointToBoneFromRefPose(const FTransform& BoneCS, const FTransform& ChildCS)
    {
        const FQuat Joint = FRotationMatrix::MakeFromXZ(ChildCS.GetLocation() - BoneCS.GetLocation(), FVector::UpVector).ToQuat();
        return Joint.Inverse() * BoneCS.GetRotation();
    }
}

void UVRPawnAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    BuildHandRig(Hands[0], TEXT("Left"));
    BuildHandRig(Hands[1], TEXT("Right"));
}

void UVRPawnAnimInstance::BuildHandRig(FHandRig& Rig, const TCHAR* Side)
{
    Rig.bReady = false;
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset) return;

    const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
    auto RefCS = [&Ref](const FName& Name, FTransform& Out)
    {
        const int32 Index = Ref.FindBoneIndex(Name);
        if (Index == INDEX_NONE) return false;
        Out = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index);
        return true;
    };

    Rig.HandBone = FName(FString::Printf(TEXT("%sHand"), Side));
    Rig.UpperArmBone = FName(FString::Printf(TEXT("%sArm"), Side));
    Rig.ForeArmBone = FName(FString::Printf(TEXT("%sForeArm"), Side));
    FTransform HandCS, MiddleCS, IndexCS, PinkyCS;
    if (!RefCS(Rig.HandBone, HandCS)
        || !RefCS(FName(FString::Printf(TEXT("%sHandMiddle1"), Side)), MiddleCS)
        || !RefCS(FName(FString::Printf(TEXT("%sHandIndex1"), Side)), IndexCS)
        || !RefCS(FName(FString::Printf(TEXT("%sHandPinky1"), Side)), PinkyCS)) return;
    Rig.PalmToBone = JointToBoneFromRefPose(HandCS, MiddleCS);

    // 메시 정점을 못 읽을 때의 근사 치수 — 본에는 두께가 없으므로 손가락 반지름 = 너클 간격(검지~새끼 3칸)의 절반,
    // 손바닥 두께 = 손가락 반지름의 1.5배.
    const float KnuckleSpan = FVector::Dist(IndexCS.GetLocation(), PinkyCS.GetLocation());
    const float ApproxRadius = KnuckleSpan / 6.f;
    const float PalmLength = FVector::Dist(HandCS.GetLocation(), MiddleCS.GetLocation());
    Rig.PalmHalfExtent = FVector(PalmLength * 0.5f, KnuckleSpan * 0.5f + ApproxRadius, ApproxRadius * 1.5f);
    Rig.PalmBoxCenter = FVector(PalmLength * 0.5f, 0.f, 0.f);
    auto RefLocal = [&Ref](const FName& Name) { return Ref.GetRefBonePose()[Ref.FindBoneIndex(Name)].GetTranslation(); };

    for (int32 Finger = 0; Finger < 5; ++Finger)
    {
        for (int32 Segment = 0; Segment < 3; ++Segment)
        {
            const int32 i = Finger * 3 + Segment;
            Rig.FingerBones[i] = FName(FString::Printf(TEXT("%sHand%s%d"), Side, FingerNames[Finger], Segment + 1));
            const FName Child(FString::Printf(TEXT("%sHand%s%d"), Side, FingerNames[Finger], Segment + 2));

            FTransform BoneCS, ChildCS;
            if (!RefCS(Rig.FingerBones[i], BoneCS) || !RefCS(Child, ChildCS)) return;
            Rig.JointToBone[i] = JointToBoneFromRefPose(BoneCS, ChildCS);
            Rig.RefLocalPos[i] = RefLocal(Rig.FingerBones[i]);
            if (Segment == 2) Rig.EndLocalPos[Finger] = RefLocal(Child);
            if (Segment >= 1)
            {
                // 캡슐 = 이 본 → 자식 본 구간(끝마디는 손가락 끝 본까지).
                const int32 j = ShapeIndex(Finger, Segment - 1);
                const FVector ChildLocal = RefLocal(Child);
                Rig.ShapeAxis[j] = ChildLocal.GetSafeNormal();
                Rig.ShapeCenter[j] = ChildLocal * 0.5f;
                Rig.ShapeRadius[j] = ApproxRadius;
                Rig.ShapeHalfHeight[j] = ChildLocal.Size() * 0.5f + ApproxRadius;
            }
        }
    }
    Rig.bReady = true;
    if (!FitHandShapesToMesh(Rig))
    {
        UE_LOG(LogTemp, Warning, TEXT("[HandTracking] %s 손 메시 정점을 못 읽어 콜라이더를 본 간격 근사로 둔다"), Side);
    }
}

bool UVRPawnAnimInstance::FitHandShapesToMesh(FHandRig& Rig)
{
    // 보이는 손 메시에 콜라이더를 맞춘다 — 정점마다 가장 크게 묶인 본을 찾아 가운데·끝마디 본 10개·손 본으로 나눠 모으고,
    // 마디는 본 로컬에서 축 방향 범위(길이)와 축에서의 거리(반지름)로 캡슐을, 손 본은 손바닥 관절 축 범위로 상자를 만든다.
    // 정점 CPU 사본이 있어야 한다(에디터는 항상 있음). 패키지 빌드에서 없으면 본 간격 근사가 남는다.
    const USkeletalMeshComponent* MeshComp = GetSkelMeshComponent();
    const USkeletalMesh* Asset = MeshComp ? MeshComp->GetSkeletalMeshAsset() : nullptr;
    const FSkeletalMeshRenderData* RenderData = Asset ? Asset->GetResourceForRendering() : nullptr;
    if (!RenderData || RenderData->LODRenderData.Num() == 0) return false;
    const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[0];
    const FPositionVertexBuffer& Positions = LOD.StaticVertexBuffers.PositionVertexBuffer;
    const FSkinWeightVertexBuffer* Skin = LOD.GetSkinWeightVertexBuffer();
    if (!Skin || !Positions.GetVertexData() || !Skin->GetDataVertexBuffer()->GetWeightData()) return false;

    const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
    constexpr int32 HandSet = NumFingerShapes;   // 0~9 = 손가락 캡슐(ShapeIndex 순), 10 = 손
    int32 TargetBones[NumFingerShapes + 1];
    for (int32 Finger = 0; Finger < 5; ++Finger)
    {
        for (int32 Shape = 0; Shape < ShapesPerFinger; ++Shape) TargetBones[ShapeIndex(Finger, Shape)] = Ref.FindBoneIndex(Rig.FingerBones[Finger * 3 + Shape + 1]);
    }
    TargetBones[HandSet] = Ref.FindBoneIndex(Rig.HandBone);
    TArray<FVector> Local[NumFingerShapes + 1];   // 대상 본 로컬 좌표
    FTransform BoneCS[NumFingerShapes + 1];
    for (int32 t = 0; t <= HandSet; ++t) BoneCS[t] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, TargetBones[t]);

    const uint32 MaxInfluences = Skin->GetMaxBoneInfluences();
    for (uint32 v = 0; v < Positions.GetNumVertices(); ++v)
    {
        int32 SectionIndex, VertexInSection;
        LOD.GetSectionFromVertexIndex(v, SectionIndex, VertexInSection);
        const TArray<FBoneIndexType>& BoneMap = LOD.RenderSections[SectionIndex].BoneMap;
        const FSkinWeightInfo Weights = Skin->GetVertexSkinWeights(v);
        uint32 Best = 0;
        for (uint32 k = 1; k < MaxInfluences; ++k)
        {
            if (Weights.InfluenceWeights[k] > Weights.InfluenceWeights[Best]) Best = k;
        }
        if (!BoneMap.IsValidIndex(Weights.InfluenceBones[Best])) continue;
        const int32 MeshBone = BoneMap[Weights.InfluenceBones[Best]];
        for (int32 t = 0; t <= HandSet; ++t)
        {
            if (MeshBone == TargetBones[t])
            {
                Local[t].Add(BoneCS[t].InverseTransformPosition(FVector(Positions.VertexPosition(v))));
                break;
            }
        }
    }
    for (const TArray<FVector>& Set : Local)
    {
        if (Set.Num() < 8) return false;
    }

    // 반지름은 상위 5% 를 버린 거리 — 손톱·주름 같은 튀는 정점 몇 개로 캡슐이 부풀지 않게.
    auto Percentile95 = [](TArray<float>& Values)
    {
        Values.Sort();
        return Values[FMath::Min(Values.Num() - 1, FMath::FloorToInt(Values.Num() * 0.95f))];
    };

    for (int32 j = 0; j < NumFingerShapes; ++j)
    {
        const FVector Axis = Rig.ShapeAxis[j];
        float MinT = TNumericLimits<float>::Max();
        float MaxT = TNumericLimits<float>::Lowest();
        FVector MeanOff = FVector::ZeroVector;
        for (const FVector& P : Local[j])
        {
            const float T = FVector::DotProduct(P, Axis);
            MinT = FMath::Min(MinT, T);
            MaxT = FMath::Max(MaxT, T);
            MeanOff += P - Axis * T;
        }
        MeanOff /= Local[j].Num();
        TArray<float> Radii;
        for (const FVector& P : Local[j]) Radii.Add((P - Axis * FVector::DotProduct(P, Axis) - MeanOff).Size());
        const float Radius = Percentile95(Radii);
        Rig.ShapeRadius[j] = Radius;
        Rig.ShapeHalfHeight[j] = FMath::Max((MaxT - MinT) * 0.5f, Radius);
        Rig.ShapeCenter[j] = Axis * ((MinT + MaxT) * 0.5f) + MeanOff;
    }

    // 손바닥 상자 — 손 본 로컬 정점을 손바닥 관절 축으로 돌려(손 본 = 손바닥 관절 × PalmToBone) 축별 범위를 잰다.
    // 손 본에 묶인 정점엔 손목·엄지 뿌리 볼록한 부분이 섞여 범위가 부풀므로 축별 상·하위 5% 를 버린다.
    // 상자 중심은 손바닥 관절이 아니라 이 범위의 중심 — 폰이 손바닥 바디에 붙인 모양으로 거기에 놓는다.
    TArray<float> Axes[3];
    for (const FVector& P : Local[HandSet])
    {
        const FVector Q = Rig.PalmToBone.RotateVector(P);
        for (int32 a = 0; a < 3; ++a) Axes[a].Add(Q[a]);
    }
    for (int32 a = 0; a < 3; ++a)
    {
        Axes[a].Sort();
        const float Lo = Axes[a][FMath::FloorToInt(Axes[a].Num() * 0.05f)];
        const float Hi = Axes[a][FMath::Min(Axes[a].Num() - 1, FMath::FloorToInt(Axes[a].Num() * 0.95f))];
        Rig.PalmBoxCenter[a] = (Lo + Hi) * 0.5f;
        Rig.PalmHalfExtent[a] = (Hi - Lo) * 0.5f;
    }

    UE_LOG(LogTemp, Log, TEXT("[HandTracking] 손 콜라이더를 메시에 맞춤 — 손바닥 반치수 %s 중심 %s, 검지 가운데 r%.2f hh%.2f · 끝 r%.2f hh%.2f"),
           *Rig.PalmHalfExtent.ToString(), *Rig.PalmBoxCenter.ToString(),
           Rig.ShapeRadius[ShapeIndex(1, 0)], Rig.ShapeHalfHeight[ShapeIndex(1, 0)], Rig.ShapeRadius[ShapeIndex(1, 1)], Rig.ShapeHalfHeight[ShapeIndex(1, 1)]);
    return true;
}

void UVRPawnAnimInstance::FingerFK(const FHandRig& Rig, const FVector& HandPos, int32 Finger, const FQuat Rel[3], FVector OutCenter[ShapesPerFinger], FQuat OutRotation[ShapesPerFinger])
{
    FVector Pos = HandPos;
    FQuat Rot = Rig.PalmToBone;
    for (int32 Segment = 0; Segment < 3; ++Segment)
    {
        const int32 i = Finger * 3 + Segment;
        Pos += Rot.RotateVector(Rig.RefLocalPos[i]);
        Rot = Rel[Segment] * Rig.JointToBone[i];
        if (Segment == 0) continue;
        // 여기서 Pos·Rot = 이 마디 본. 캡슐 중심·축은 그 본 로컬 값(메시 정점에 맞춘 것)을 옮긴다.
        const int32 Shape = Segment - 1;
        const int32 j = ShapeIndex(Finger, Shape);
        OutCenter[Shape] = Pos + Rot.RotateVector(Rig.ShapeCenter[j]);
        OutRotation[Shape] = FRotationMatrix::MakeFromZ(Rot.RotateVector(Rig.ShapeAxis[j])).ToQuat();
    }
}

void UVRPawnAnimInstance::UpdateFingertipTargets(FHandRig& Rig, const FXRHandTrackingState& State, TConstArrayView<const UPrimitiveComponent*> Obstacles, const FTransform& PalmWorld)
{
    // 손바닥 관절 기준(= 손바닥 바디 기준) 상대 좌표로 메시 손가락을 FK 한다. 트래킹 관절만 쓰고 메시·물리 결과는 안 쓴다 —
    // 메시 손(FBIK, 한 프레임 늦음)에서 출발하면 손이 밀릴 때 손끝 모양이 손바닥 기준으로 흔들려 표면에 박혔다 빠졌다 떤다.
    // 손 본 = 손목 관절 위치(이펙터와 같은 오프셋) + 손바닥 보정 회전, 손가락 = 레퍼런스 본 길이 + 트래킹 회전.
    // ponytail: 메시 컴포넌트 스케일 1 가정 — 몸 메시를 키우면 본 길이에 스케일을 곱할 것.
    const FQuat PalmKey = State.HandKeyRotations[static_cast<int32>(EHandKeypoint::Palm)];
    const FQuat PalmInverse = PalmKey.Inverse();
    const FVector HandPos = PalmInverse.RotateVector(State.HandKeyLocations[static_cast<int32>(EHandKeypoint::Wrist)]
                                                   - State.HandKeyLocations[static_cast<int32>(EHandKeypoint::Palm)]);

    // 감싸기 — 실제 손은 물체가 없어 끝까지 오므라지지만 가상 물체에 막힌 손가락 캡슐은 손바닥에 용접돼 따로 못 휜다.
    // 트래킹대로 물체 속에 옮기면 엔진이 손 전체를 밀어내고 다음 틱에 또 박혀 버벅인다. 그래서 관절별로 고정한다:
    //   가운데 마디가 닿으면 첫째·둘째 관절, 끝마디가 닿으면 세 관절 모두를 직전 프레임(안 닿았던) 각도에 고정 →
    //   고정 안 된 끝 관절은 계속 트래킹을 따라 굽다가 닿으면 멈춘다(감싸 쥔 모양).
    //   고정한 관절은 실제 손가락이 고정 각도보다 UnfreezeSlack 넘게 펴지면 푼다. 근처에 물체가 없으면 모두 푼다.
    // 충돌은 그대로 — 고정은 캡슐이 물체 속으로 순간이동하지 않게 할 뿐이다.
    static constexpr float UnfreezeSlack = 0.087f;   // 5°(라디안)
    auto Curl = [](const FQuat& Local) { return FQuat::Identity.AngularDistance(Local); };
    auto Hits = [&](const FVector& Center, const FQuat& Rotation, const FCollisionShape& Shape)
    {
        const FVector WorldCenter = PalmWorld.TransformPosition(Center);
        const FQuat WorldRotation = PalmWorld.GetRotation() * Rotation;
        for (const UPrimitiveComponent* Obstacle : Obstacles)
        {
            if (Obstacle->OverlapComponent(WorldCenter, WorldRotation, Shape)) return true;
        }
        return false;
    };

    for (int32 Finger = 0; Finger < 5; ++Finger)
    {
        // 트래킹 관절 로컬 회전(부모 관절 기준, 첫 관절은 손바닥 관절 기준).
        FQuat Tracked[3];
        FQuat TrackedRel[3];
        FQuat Parent = FQuat::Identity;
        for (int32 Segment = 0; Segment < 3; ++Segment)
        {
            TrackedRel[Segment] = PalmInverse * State.HandKeyRotations[KeyIndex(Finger, Segment)];
            Tracked[Segment] = Parent.Inverse() * TrackedRel[Segment];
            Parent = TrackedRel[Segment];
        }

        // 풀기 — 근처에 물체가 없거나, 실제 손가락 모양 그대로 놓아도 안 겹치면(툭 치고 뗌) 손가락 전체를,
        // 실제 손가락이 고정 각도보다 펴졌으면 그 관절을. 감싸 쥔 동안은 실제 손가락이 물체 속까지 오므라져 계속 겹친다.
        bool bAnyFrozen = false;
        for (int32 Segment = 0; Segment < 3; ++Segment) bAnyFrozen |= Rig.bFrozen[Finger * 3 + Segment];
        bool bTrackedClear = Obstacles.IsEmpty();
        if (bAnyFrozen && !bTrackedClear)
        {
            FVector TrackedCenters[ShapesPerFinger];
            FQuat TrackedRotations[ShapesPerFinger];
            FingerFK(Rig, HandPos, Finger, TrackedRel, TrackedCenters, TrackedRotations);
            bTrackedClear = true;
            for (int32 Shape = 0; Shape < ShapesPerFinger && bTrackedClear; ++Shape)
            {
                const int32 j = ShapeIndex(Finger, Shape);
                bTrackedClear = !Hits(TrackedCenters[Shape], TrackedRotations[Shape], FCollisionShape::MakeCapsule(Rig.ShapeRadius[j], Rig.ShapeHalfHeight[j]));
            }
        }
        for (int32 Segment = 0; Segment < 3; ++Segment)
        {
            const int32 i = Finger * 3 + Segment;
            if (Rig.bFrozen[i] && (bTrackedClear || Curl(Tracked[Segment]) < Curl(Rig.FrozenLocal[i]) - UnfreezeSlack)) Rig.bFrozen[i] = false;
        }

        FQuat Local[3];
        FQuat Rel[3];
        FVector* Centers = &Rig.TargetCenter[ShapeIndex(Finger, 0)];
        FQuat* Rotations = &Rig.TargetRotation[ShapeIndex(Finger, 0)];
        auto Build = [&]()
        {
            FQuat Chain = FQuat::Identity;
            for (int32 Segment = 0; Segment < 3; ++Segment)
            {
                const int32 i = Finger * 3 + Segment;
                Local[Segment] = Rig.bFrozen[i] ? Rig.FrozenLocal[i] : Tracked[Segment];
                Chain = Chain * Local[Segment];
                Rel[Segment] = Chain;
            }
            FingerFK(Rig, HandPos, Finger, Rel, Centers, Rotations);
        };
        Build();

        // 고정 — 닿은 마디를 움직이는 관절을 직전 프레임 각도에(처음 프레임이면 지금 각도에).
        if (!Obstacles.IsEmpty())
        {
            int32 FreezeUpTo = -1;   // 이 관절까지 고정
            for (int32 Shape = ShapesPerFinger - 1; Shape >= 0 && FreezeUpTo < 0; --Shape)
            {
                const int32 j = ShapeIndex(Finger, Shape);
                if (Hits(Centers[Shape], Rotations[Shape], FCollisionShape::MakeCapsule(Rig.ShapeRadius[j], Rig.ShapeHalfHeight[j]))) FreezeUpTo = Shape + 1;
            }
            bool bChanged = false;
            for (int32 Segment = 0; Segment <= FreezeUpTo; ++Segment)
            {
                const int32 i = Finger * 3 + Segment;
                if (Rig.bFrozen[i]) continue;
                Rig.bFrozen[i] = true;
                Rig.FrozenLocal[i] = Rig.bLastValid ? Rig.LastLocal[i] : Local[Segment];
                bChanged = true;
            }
            if (bChanged) Build();
        }

        for (int32 Segment = 0; Segment < 3; ++Segment)
        {
            Rig.LastLocal[Finger * 3 + Segment] = Local[Segment];
            Rig.KeyRotations[KeyIndex(Finger, Segment)] = PalmKey * Rel[Segment];
        }
    }
    Rig.bLastValid = true;
    Rig.bTipTargets = true;
}

void UVRPawnAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    // 관절 데이터는 게임 스레드에서 복사해 두고, 워커 스레드 평가(프록시)는 이 복사본만 읽는다.
    // 손 근처 아이템에 닿은 마디는 관절별로 고정한다(UpdateFingertipTargets). 벽 등 나머지는 손 전체 충돌로 막힌다.
    const AVRPawn* Pawn = Cast<AVRPawn>(TryGetPawnOwner());
    for (int32 h = 0; h < 2; ++h)
    {
        FHandRig& Rig = Hands[h];
        Rig.KeyRotations.Reset();
        Rig.bTipTargets = false;
        Rig.bEffector = false;
        if (!Pawn || !Rig.bReady) continue;

        // 팔 IK 목표 — 핸드트래킹·컨트롤러 공통(이펙터는 둘 다 물리 손바닥을 따른다).
        Rig.EffectorCS = h == 0 ? Pawn->GetLeftHandEffectorCS() : Pawn->GetRightHandEffectorCS();
        Rig.bEffector = true;

        const EControllerHand Hand = h == 0 ? EControllerHand::Left : EControllerHand::Right;
        const FXRHandTrackingState& State = Pawn->GetHandTrackState(Hand);
        if (!State.bValid || State.HandKeyRotations.Num() != EHandKeypointCount)
        {
            // 트래킹이 끊기면 고정도 끝 — 다시 잡혔을 때 옛 각도로 굳지 않게.
            Rig.bLastValid = false;
            for (bool& bFrozen : Rig.bFrozen) bFrozen = false;
            continue;
        }

        const UBoxComponent* Palm = h == 0 ? Pawn->PhysicsPalmLeft : Pawn->PhysicsPalmRight;
        TArray<const UPrimitiveComponent*, TInlineAllocator<4>> Obstacles;
        if (IsValid(Palm)) Pawn->GetNearbyItemMeshes(Hand, Obstacles);
        Rig.KeyRotations = State.HandKeyRotations;
        UpdateFingertipTargets(Rig, State, Obstacles, IsValid(Palm) ? Palm->GetComponentTransform() : FTransform::Identity);
    }
}

FQuat UVRPawnAnimInstance::GetPalmToHandBone(EControllerHand Hand) const
{
    return Hands[Hand == EControllerHand::Left ? 0 : 1].PalmToBone;
}

bool UVRPawnAnimInstance::GetFingerShapeTarget(EControllerHand Hand, int32 Finger, int32 Shape, FVector& OutCenter, FQuat& OutRotation) const
{
    const FHandRig& Rig = Hands[Hand == EControllerHand::Left ? 0 : 1];
    if (!Rig.bTipTargets || Finger < 0 || Finger >= 5 || Shape < 0 || Shape >= ShapesPerFinger) return false;
    OutCenter = Rig.TargetCenter[ShapeIndex(Finger, Shape)];
    OutRotation = Rig.TargetRotation[ShapeIndex(Finger, Shape)];
    return true;
}

FAnimInstanceProxy* UVRPawnAnimInstance::CreateAnimInstanceProxy()
{
    return new FVRPawnAnimInstanceProxy(this);
}

bool FVRPawnAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
    EvaluateAnimationNode_WithRoot(Output, InRootNode);

    // 링크드 레이어 등 하위 그래프 평가에는 손대지 않는다 — 최종 포즈에만 한 번 적용.
    const UVRPawnAnimInstance* Instance = Cast<UVRPawnAnimInstance>(GetAnimInstanceObject());
    if (!Instance || InRootNode != GetRootNode()) return true;

    const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
    auto PoseIndexOf = [&Bones](const FName& Name)
    {
        const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
        return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
    };

    // 팔 3관절 IK — FBIK 는 머리·양손·양발 목표를 절충하는 풀이라 손을 이펙터에 정확히 못 붙인다(반복 60 에서 평균 1cm,
    // 팔을 뻗으면 6~8cm). 그만큼 손 콜라이더(물리 손바닥)와 보이는 손이 어긋나므로 어깨~손목을 다시 풀어 손을 목표에 붙인다.
    // 팔꿈치 방향은 FBIK 결과를 극점으로 그대로 쓴다. 손 본만 옮기면 아래팔과 손 사이에서 손목이 꺾여 보였다.
    {
        FCSPose<FCompactPose> CSPose;
        CSPose.InitPose(Output.Pose);
        TArray<FBoneTransform> Arms;
        for (const UVRPawnAnimInstance::FHandRig& Rig : Instance->Hands)
        {
            if (!Rig.bEffector) continue;
            const FCompactPoseBoneIndex Upper = PoseIndexOf(Rig.UpperArmBone);
            const FCompactPoseBoneIndex Fore = PoseIndexOf(Rig.ForeArmBone);
            const FCompactPoseBoneIndex Hand = PoseIndexOf(Rig.HandBone);
            if (!Upper.IsValid() || !Fore.IsValid() || !Hand.IsValid()) continue;

            FTransform UpperCS = CSPose.GetComponentSpaceTransform(Upper);
            FTransform ForeCS = CSPose.GetComponentSpaceTransform(Fore);
            FTransform HandCS = CSPose.GetComponentSpaceTransform(Hand);
            AnimationCore::SolveTwoBoneIK(UpperCS, ForeCS, HandCS, ForeCS.GetLocation(), Rig.EffectorCS.GetLocation(),
                                          /*bAllowStretching=*/false, 1.0, 1.0);
            HandCS.SetRotation(Rig.EffectorCS.GetRotation());
            Arms.Add(FBoneTransform(Upper, UpperCS));
            Arms.Add(FBoneTransform(Fore, ForeCS));
            Arms.Add(FBoneTransform(Hand, HandCS));
        }
        if (Arms.Num() > 0)
        {
            Arms.Sort([](const FBoneTransform& A, const FBoneTransform& B) { return A.BoneIndex < B.BoneIndex; });
            CSPose.SafeSetCSBoneTransforms(Arms);
            FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Output.Pose);
        }
    }

    for (const UVRPawnAnimInstance::FHandRig& Rig : Instance->Hands)
    {
        const TArray<FQuat>& Key = Rig.KeyRotations;
        if (Key.Num() != EHandKeypointCount) continue;

        // 손가락 첫 본의 부모는 손 본(= 손바닥 관절 × 손 보정)이므로 로컬 회전만으로 풀린다 —
        // 손 본의 실제 컴포넌트 공간 회전(FBIK 결과)이 필요 없다. 각 본은 실제 관절의 월드 방향을 그대로 따른다.
        const FQuat HandFrame = Key[static_cast<int32>(EHandKeypoint::Palm)] * Rig.PalmToBone;
        for (int32 Finger = 0; Finger < 5; ++Finger)
        {
            FQuat ParentFrame = HandFrame;
            for (int32 Segment = 0; Segment < 3; ++Segment)
            {
                const int32 i = Finger * 3 + Segment;
                const FQuat Frame = Key[KeyIndex(Finger, Segment)] * Rig.JointToBone[i];
                const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Rig.FingerBones[i]);
                if (MeshIndex != INDEX_NONE)
                {
                    const FCompactPoseBoneIndex PoseIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
                    if (PoseIndex.IsValid())
                    {
                        Output.Pose[PoseIndex].SetRotation((ParentFrame.Inverse() * Frame).GetNormalized());
                    }
                }
                ParentFrame = Frame;
            }
        }
    }
    return true;
}
