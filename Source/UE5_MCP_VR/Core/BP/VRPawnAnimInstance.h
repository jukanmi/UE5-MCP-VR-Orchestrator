#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "InputCoreTypes.h"
#include "HeadMountedDisplayTypes.h"
#include "VRPawnAnimInstance.generated.h"

/**
 * VR 폰 몸 메시 AnimBP 의 부모 클래스.
 * AnimBP 그래프(FBIK 포함)를 평가한 뒤, 핸드트래킹이 추적 중인 손의 손가락 본 회전을
 * OpenXR 관절 회전으로 덮어쓴다. 그래프에 노드를 추가할 필요가 없다.
 *
 * 관절 프레임 → 본 프레임 보정은 레퍼런스 포즈에서 자동 계산한다 — 본→자식 방향을 손끝 방향,
 * 월드 위(+Z)를 손등 방향으로 본다. 실측 캘리브레이션은 쓰지 않는다: 실제 손을 펴도 손가락마다
 * 비틀림·벌어짐이 메시와 달라, 그 차이가 보정에 구워지면 굽힘 축이 돌아가 주먹 쥘 때 손가락이 옆으로 말린다.
 */
UCLASS()
class UE5_MCP_VR_API UVRPawnAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    /** 손가락 수 × 손가락당 본 수. 엄지는 Metacarpal·Proximal·Distal, 나머지는 Proximal·Intermediate·Distal. */
    static constexpr int32 NumFingerBones = 15;

    /** 손가락당 캡슐 수 — 0 = 가운데 마디(둘째 본), 1 = 끝마디(셋째 본). 엄지는 첫 마디·끝마디. 첫 마디는 손바닥 상자와 겹쳐 두지 않는다. */
    static constexpr int32 ShapesPerFinger = 2;
    static constexpr int32 NumFingerShapes = 5 * ShapesPerFinger;
    static int32 ShapeIndex(int32 Finger, int32 Shape) { return Finger * ShapesPerFinger + Shape; }

    /** 손바닥 관절 프레임 → 손 본 프레임 보정. 이펙터 회전 = 손바닥 관절 회전 × 이 값. */
    FQuat GetPalmToHandBone(EControllerHand Hand) const;

    /** 손가락 캡슐의 중점·회전(캡슐 Z 축 = 그 마디 방향), 손바닥 관절(= 손바닥 바디) 기준 상대값. 트래킹 자세 기준. */
    bool GetFingerShapeTarget(EControllerHand Hand, int32 Finger, int32 Shape, FVector& OutCenter, FQuat& OutRotation) const;

    // 손 콜라이더 치수 — 보이는 손 메시 정점에 맞춘 값(정점을 못 읽으면 본 간격 근사). 메시를 바꿔도 따라온다.
    bool HasHandShapes() const { return Hands[0].bReady && Hands[1].bReady; }
    float GetShapeRadius(EControllerHand Hand, int32 Finger, int32 Shape) const { return RigOf(Hand).ShapeRadius[ShapeIndex(Finger, Shape)]; }
    /** 캡슐 절반 길이(반구 포함). */
    float GetShapeHalfHeight(EControllerHand Hand, int32 Finger, int32 Shape) const { return RigOf(Hand).ShapeHalfHeight[ShapeIndex(Finger, Shape)]; }
    /** 손바닥 상자 절반 치수(손바닥 관절 축: X=손끝, Y=옆, Z=손등). */
    FVector GetPalmHalfExtent(EControllerHand Hand) const { return RigOf(Hand).PalmHalfExtent; }
    /** 손바닥 상자 중심 — 손 본 원점(손목) 기준, 손바닥 관절 축. */
    FVector GetPalmBoxCenter(EControllerHand Hand) const { return RigOf(Hand).PalmBoxCenter; }

protected:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
    friend struct FVRPawnAnimInstanceProxy;

    struct FHandRig
    {
        FName HandBone;
        FName FingerBones[NumFingerBones];
        FQuat PalmToBone = FQuat::Identity;
        FQuat JointToBone[NumFingerBones];
        /** 레퍼런스 포즈 로컬 위치(부모 본 기준) — 손가락 본 15개와 손가락 끝 본 5개. 메시 손가락 FK 용. */
        FVector RefLocalPos[NumFingerBones];
        FVector EndLocalPos[5];
        /** 손가락 캡슐(가운데·끝마디 본 로컬: 중심·축)과 치수, 손바닥 상자 절반 치수. */
        FVector ShapeCenter[NumFingerShapes];
        FVector ShapeAxis[NumFingerShapes];
        float ShapeRadius[NumFingerShapes];
        float ShapeHalfHeight[NumFingerShapes];
        FVector PalmHalfExtent = FVector(4.5f, 4.25f, 1.5f);
        FVector PalmBoxCenter = FVector::ZeroVector;
        /** 이번 프레임 손가락 캡슐 목표(손바닥 기준 상대 위치·회전). */
        FVector TargetCenter[NumFingerShapes];
        FQuat TargetRotation[NumFingerShapes];
        bool bTipTargets = false;
        /** 관절별 고정 — 마디가 물체에 닿으면 그 마디를 움직이는 관절을 직전 프레임 각도(부모 관절 기준 로컬 회전)에 고정한다.
         *  실제 손가락이 고정 각도보다 펴지면 푼다. LastLocal = 직전 프레임에 실제로 쓴 관절 로컬 회전. */
        FQuat LastLocal[NumFingerBones];
        FQuat FrozenLocal[NumFingerBones];
        bool bFrozen[NumFingerBones] = {};
        bool bLastValid = false;
        /** 이번 프레임 관절 월드 회전(EHandKeypoint 순). 비어 있으면 미추적 — 그래프 포즈 그대로 둔다. */
        TArray<FQuat> KeyRotations;
        /** 팔 3관절 IK — 위팔·아래팔 본과 이번 프레임 손 이펙터(몸 메시 공간, FBIK 에 넣는 것과 같은 값). */
        FName UpperArmBone;
        FName ForeArmBone;
        FTransform EffectorCS;
        bool bEffector = false;
        bool bReady = false;
    };

    void BuildHandRig(FHandRig& Rig, const TCHAR* Side);

    /** 손 메시 정점(가운데·끝마디 본·손 본에 가장 크게 묶인 정점)으로 손가락 캡슐·손바닥 상자를 맞춘다. 정점을 못 읽으면 false. */
    bool FitHandShapesToMesh(FHandRig& Rig);

    const FHandRig& RigOf(EControllerHand Hand) const { return Hands[Hand == EControllerHand::Left ? 0 : 1]; }

    /** 트래킹 관절만으로 손바닥 기준 메시 손가락을 FK 해 손가락 캡슐 목표를 채운다.
     *  Obstacles(손 근처 아이템)에 마디가 닿으면 관절별로 고정한다(감싸기) — Rig.KeyRotations 도 그 자세로 바꾼다.
     *  PalmWorld = 손바닥 바디 월드 변환(손가락 캡슐이 붙는 기준). */
    void UpdateFingertipTargets(FHandRig& Rig, const FXRHandTrackingState& State, TConstArrayView<const UPrimitiveComponent*> Obstacles, const FTransform& PalmWorld);

    /** 손가락 하나 FK — Rel = 관절 3개의 손바닥 관절 기준 회전, HandPos = 손바닥 관절 기준 손 본 위치. 캡슐 2개(가운데·끝마디)의 중점·회전(손바닥 기준)을 낸다. */
    static void FingerFK(const FHandRig& Rig, const FVector& HandPos, int32 Finger, const FQuat Rel[3], FVector OutCenter[ShapesPerFinger], FQuat OutRotation[ShapesPerFinger]);

    FHandRig Hands[2];
};

USTRUCT()
struct FVRPawnAnimInstanceProxy : public FAnimInstanceProxy
{
    GENERATED_BODY()

    FVRPawnAnimInstanceProxy() = default;
    explicit FVRPawnAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

protected:
    virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;
};
