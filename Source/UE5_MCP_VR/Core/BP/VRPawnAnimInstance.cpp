#include "Core/BP/VRPawnAnimInstance.h"
#include "Core/BP/VRPawn.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HeadMountedDisplayTypes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "HeadMountedDisplayFunctionLibrary.h"
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
            if (Segment == 2)
            {
                Rig.EndLocalPos[Finger] = RefLocal(Child);
                Rig.TipShapeAxis[Finger] = Rig.EndLocalPos[Finger].GetSafeNormal();
                Rig.TipShapeCenter[Finger] = Rig.EndLocalPos[Finger] * 0.5f;
                Rig.TipRadius[Finger] = ApproxRadius;
                Rig.TipHalfHeight[Finger] = Rig.EndLocalPos[Finger].Size() * 0.5f + ApproxRadius;
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
    // 보이는 손 메시에 콜라이더를 맞춘다 — 정점마다 가장 크게 묶인 본을 찾아 끝마디 본 5개·손 본으로 나눠 모으고,
    // 끝마디는 본 로컬에서 축 방향 범위(길이)와 축에서의 거리(반지름)로 캡슐을, 손 본은 손바닥 관절 축 범위로 상자를 만든다.
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
    int32 TargetBones[6];   // 0~4 = 손가락 끝마디, 5 = 손
    for (int32 Finger = 0; Finger < 5; ++Finger) TargetBones[Finger] = Ref.FindBoneIndex(Rig.FingerBones[Finger * 3 + 2]);
    TargetBones[5] = Ref.FindBoneIndex(Rig.HandBone);
    TArray<FVector> Local[6];   // 대상 본 로컬 좌표
    FTransform BoneCS[6];
    for (int32 t = 0; t < 6; ++t) BoneCS[t] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, TargetBones[t]);

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
        for (int32 t = 0; t < 6; ++t)
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

    for (int32 Finger = 0; Finger < 5; ++Finger)
    {
        const FVector Axis = Rig.TipShapeAxis[Finger];
        float MinT = TNumericLimits<float>::Max();
        float MaxT = TNumericLimits<float>::Lowest();
        FVector MeanOff = FVector::ZeroVector;
        for (const FVector& P : Local[Finger])
        {
            const float T = FVector::DotProduct(P, Axis);
            MinT = FMath::Min(MinT, T);
            MaxT = FMath::Max(MaxT, T);
            MeanOff += P - Axis * T;
        }
        MeanOff /= Local[Finger].Num();
        TArray<float> Radii;
        for (const FVector& P : Local[Finger]) Radii.Add((P - Axis * FVector::DotProduct(P, Axis) - MeanOff).Size());
        const float Radius = Percentile95(Radii);
        Rig.TipRadius[Finger] = Radius;
        Rig.TipHalfHeight[Finger] = FMath::Max((MaxT - MinT) * 0.5f, Radius);
        Rig.TipShapeCenter[Finger] = Axis * ((MinT + MaxT) * 0.5f) + MeanOff;
    }

    // 손바닥 상자 — 손 본 로컬 정점을 손바닥 관절 축으로 돌려(손 본 = 손바닥 관절 × PalmToBone) 축별 범위를 잰다.
    // 손 본에 묶인 정점엔 손목·엄지 뿌리 볼록한 부분이 섞여 범위가 부풀므로 축별 상·하위 5% 를 버린다.
    // 상자 중심은 손바닥 관절이 아니라 이 범위의 중심 — 폰이 손바닥 바디에 붙인 모양으로 거기에 놓는다.
    TArray<float> Axes[3];
    for (const FVector& P : Local[5])
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

    UE_LOG(LogTemp, Log, TEXT("[HandTracking] 손 콜라이더를 메시에 맞춤 — 손바닥 반치수 %s 중심 %s, 검지 끝 r%.2f hh%.2f"),
           *Rig.PalmHalfExtent.ToString(), *Rig.PalmBoxCenter.ToString(), Rig.TipRadius[1], Rig.TipHalfHeight[1]);
    return true;
}

void UVRPawnAnimInstance::UpdateFingertipTargets(FHandRig& Rig, const FXRHandTrackingState& State)
{
    // 손바닥 관절 기준(= 손바닥 바디 기준) 상대 좌표로 메시 손가락을 FK 한다. 트래킹 관절만 쓰고 메시·물리 결과는 안 쓴다 —
    // 메시 손(FBIK, 한 프레임 늦음)에서 출발하면 손이 밀릴 때 손끝 모양이 손바닥 기준으로 흔들려 표면에 박혔다 빠졌다 떤다.
    // 손 본 = 손목 관절 위치(이펙터와 같은 오프셋) + 손바닥 보정 회전, 손가락 = 레퍼런스 본 길이 + 트래킹 회전.
    // ponytail: 메시 컴포넌트 스케일 1 가정 — 몸 메시를 키우면 본 길이에 스케일을 곱할 것.
    const FQuat PalmInverse = State.HandKeyRotations[static_cast<int32>(EHandKeypoint::Palm)].Inverse();
    const FVector HandPos = PalmInverse.RotateVector(State.HandKeyLocations[static_cast<int32>(EHandKeypoint::Wrist)]
                                                   - State.HandKeyLocations[static_cast<int32>(EHandKeypoint::Palm)]);
    for (int32 Finger = 0; Finger < 5; ++Finger)
    {
        FVector Pos = HandPos;
        FQuat Rot = Rig.PalmToBone;
        for (int32 Segment = 0; Segment < 3; ++Segment)
        {
            const int32 i = Finger * 3 + Segment;
            Pos += Rot.RotateVector(Rig.RefLocalPos[i]);
            Rot = PalmInverse * State.HandKeyRotations[KeyIndex(Finger, Segment)] * Rig.JointToBone[i];
        }
        // 여기서 Pos·Rot = 끝마디 본. 캡슐 중심·축은 그 본 로컬 값(메시 정점에 맞춘 것)을 옮긴다.
        Rig.TipCenter[Finger] = Pos + Rot.RotateVector(Rig.TipShapeCenter[Finger]);
        Rig.TipRotation[Finger] = FRotationMatrix::MakeFromZ(Rot.RotateVector(Rig.TipShapeAxis[Finger])).ToQuat();
    }
    Rig.bTipTargets = true;
}

void UVRPawnAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    // 관절 데이터는 게임 스레드에서 복사해 두고, 워커 스레드 평가(프록시)는 이 복사본만 읽는다.
    // 손끝이 표면에 막히는 건 손바닥에 용접된 손끝 모양이 물리로 처리한다(손 전체가 멈추거나 들림) — 여기선 트래킹 그대로.
    const AVRPawn* Pawn = Cast<AVRPawn>(TryGetPawnOwner());
    // 손 본은 FBIK 결과 대신 물리 손바닥이 가리키는 이펙터 목표에 강제로 둔다 — FBIK 가 목표에 딱 닿지 않으면
    // 보이는 손과 콜라이더(같은 목표 기준으로 계산)가 몇 cm 어긋나, 벽에 닿기 전에 멈춘 것처럼 보인다.
    // 헤드셋이 없으면 FBIK 가 돌지 않아 손만 팔에서 떨어져 보이므로 HMD 가 켜졌을 때만.
    const bool bForceHands = Pawn && UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
    for (int32 h = 0; h < 2; ++h)
    {
        FHandRig& Rig = Hands[h];
        Rig.KeyRotations.Reset();
        Rig.bTipTargets = false;
        Rig.bForceHand = bForceHands && Rig.bReady;
        if (Rig.bForceHand) Rig.HandTargetCS = h == 0 ? Pawn->GetLeftHandEffectorCS() : Pawn->GetRightHandEffectorCS();
        if (!Pawn || !Rig.bReady) continue;

        const FXRHandTrackingState& State = Pawn->GetHandTrackState(h == 0 ? EControllerHand::Left : EControllerHand::Right);
        if (!State.bValid || State.HandKeyRotations.Num() != EHandKeypointCount) continue;

        Rig.KeyRotations = State.HandKeyRotations;
        UpdateFingertipTargets(Rig, State);
    }
}

FQuat UVRPawnAnimInstance::GetPalmToHandBone(EControllerHand Hand) const
{
    return Hands[Hand == EControllerHand::Left ? 0 : 1].PalmToBone;
}

bool UVRPawnAnimInstance::GetFingertipTarget(EControllerHand Hand, int32 Finger, FVector& OutCenter, FQuat& OutRotation) const
{
    const FHandRig& Rig = Hands[Hand == EControllerHand::Left ? 0 : 1];
    if (!Rig.bTipTargets || Finger < 0 || Finger >= 5) return false;
    OutCenter = Rig.TipCenter[Finger];
    OutRotation = Rig.TipRotation[Finger];
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

    // 손 본을 목표 컴포넌트 공간 변환에 맞춘다 — 로컬 = 목표 × 부모(팔뚝) 컴포넌트 공간⁻¹. 손가락 로컬은 그대로라 같이 따라온다.
    if (Instance->Hands[0].bForceHand || Instance->Hands[1].bForceHand)
    {
        UVRPawnAnimInstance* Diag = const_cast<UVRPawnAnimInstance*>(Instance);
        FCSPose<FCompactPose> CSPose;
        CSPose.InitPose(Output.Pose);
        for (int32 h = 0; h < 2; ++h)
        {
            const UVRPawnAnimInstance::FHandRig& Rig = Instance->Hands[h];
            if (!Rig.bForceHand) continue;
            const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Rig.HandBone);
            if (MeshIndex == INDEX_NONE) continue;
            const FCompactPoseBoneIndex HandIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
            if (!HandIndex.IsValid()) continue;
            const FCompactPoseBoneIndex ParentIndex = Bones.GetParentBoneIndex(HandIndex);
            if (!ParentIndex.IsValid()) continue;

            const FTransform Fbik = CSPose.GetComponentSpaceTransform(HandIndex);
            const float PosError = FVector::Dist(Fbik.GetLocation(), Rig.HandTargetCS.GetLocation());
            const float AngleError = FMath::RadiansToDegrees(Fbik.GetRotation().AngularDistance(Rig.HandTargetCS.GetRotation()));
            (h == 0 ? Diag->FbikHandPosErrorLeft : Diag->FbikHandPosErrorRight) = PosError;
            (h == 0 ? Diag->FbikHandAngleErrorLeft : Diag->FbikHandAngleErrorRight) = AngleError;

            FTransform Target = Fbik;
            if (Instance->bSnapHandPosition) Target.SetLocation(Rig.HandTargetCS.GetLocation());
            if (Instance->bSnapHandRotation) Target.SetRotation(Rig.HandTargetCS.GetRotation());
            FTransform Local = Target.GetRelativeTransform(CSPose.GetComponentSpaceTransform(ParentIndex));
            Local.SetScale3D(Output.Pose[HandIndex].GetScale3D());
            Output.Pose[HandIndex] = Local;
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
