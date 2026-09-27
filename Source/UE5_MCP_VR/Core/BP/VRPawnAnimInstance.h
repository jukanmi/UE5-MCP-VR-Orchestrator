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

    /** 손바닥 관절 프레임 → 손 본 프레임 보정. 이펙터 회전 = 손바닥 관절 회전 × 이 값. */
    FQuat GetPalmToHandBone(EControllerHand Hand) const;

    /** 손끝 캡슐의 중점·회전(캡슐 Z 축 = 손가락 끝마디 방향), 손바닥 관절(= 손바닥 바디) 기준 상대값. 트래킹 자세 기준. */
    bool GetFingertipTarget(EControllerHand Hand, int32 Finger, FVector& OutCenter, FQuat& OutRotation) const;

    // 손 콜라이더 치수 — 보이는 손 메시 정점에 맞춘 값(정점을 못 읽으면 본 간격 근사). 메시를 바꿔도 따라온다.
    bool HasHandShapes() const { return Hands[0].bReady && Hands[1].bReady; }
    float GetTipRadius(EControllerHand Hand, int32 Finger) const { return RigOf(Hand).TipRadius[Finger]; }
    /** 캡슐 절반 길이(반구 포함). */
    float GetTipHalfHeight(EControllerHand Hand, int32 Finger) const { return RigOf(Hand).TipHalfHeight[Finger]; }
    /** 손바닥 상자 절반 치수(손바닥 관절 축: X=손끝, Y=옆, Z=손등). */
    FVector GetPalmHalfExtent(EControllerHand Hand) const { return RigOf(Hand).PalmHalfExtent; }
    /** 손바닥 상자 중심 — 손 본 원점(손목) 기준, 손바닥 관절 축. */
    FVector GetPalmBoxCenter(EControllerHand Hand) const { return RigOf(Hand).PalmBoxCenter; }

    /** FBIK 뒤 손 본 위치를 콜라이더 기준(이펙터 목표)으로 강제할지. PIE 중 즉시 반영. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|HandTracking")
    bool bSnapHandPosition = true;

    /** FBIK 뒤 손 본 회전을 콜라이더 기준(이펙터 목표)으로 강제할지. FBIK 가 회전은 0.2° 안으로 맞추므로 기본 끔 —
     *  켜면 강제된 손에서 다음 프레임 FBIK 가 출발해 팔이 비틀린 해로 풀릴 수 있다(헤드셋에서 손목 꺾임 확인). PIE 중 즉시 반영. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|HandTracking")
    bool bSnapHandRotation = false;

    // 진단용 — 강제 전 FBIK 손 본이 이펙터 목표에서 벗어난 정도(워커 스레드가 매 평가 기록). 왼손·오른손.
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "VR|HandTracking")
    float FbikHandPosErrorLeft = 0.f;
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "VR|HandTracking")
    float FbikHandAngleErrorLeft = 0.f;
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "VR|HandTracking")
    float FbikHandPosErrorRight = 0.f;
    UPROPERTY(VisibleInstanceOnly, Transient, Category = "VR|HandTracking")
    float FbikHandAngleErrorRight = 0.f;

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
        /** 손끝 캡슐(끝마디 본 로컬: 중심·축)과 치수, 손바닥 상자 절반 치수. */
        FVector TipShapeCenter[5];
        FVector TipShapeAxis[5];
        float TipRadius[5];
        float TipHalfHeight[5];
        FVector PalmHalfExtent = FVector(4.5f, 4.25f, 1.5f);
        FVector PalmBoxCenter = FVector::ZeroVector;
        /** 이번 프레임 메시 끝마디 목표(손끝 모양의 손바닥 기준 상대 위치·회전). */
        FVector TipCenter[5];
        FQuat TipRotation[5];
        bool bTipTargets = false;
        /** 이번 프레임 관절 월드 회전(EHandKeypoint 순). 비어 있으면 미추적 — 그래프 포즈 그대로 둔다. */
        TArray<FQuat> KeyRotations;
        /** 손 본을 강제로 둘 컴포넌트 공간 변환(= 물리 손바닥이 가리키는 이펙터 목표). bForceHand 일 때만 적용. */
        FTransform HandTargetCS;
        bool bForceHand = false;
        bool bReady = false;
    };

    void BuildHandRig(FHandRig& Rig, const TCHAR* Side);

    /** 손 메시 정점(끝마디 본·손 본에 가장 크게 묶인 정점)으로 손끝 캡슐·손바닥 상자를 맞춘다. 정점을 못 읽으면 false. */
    bool FitHandShapesToMesh(FHandRig& Rig);

    const FHandRig& RigOf(EControllerHand Hand) const { return Hands[Hand == EControllerHand::Left ? 0 : 1]; }

    /** 트래킹 관절만으로 손바닥 기준 메시 손가락을 FK 해 끝마디 목표를 채운다. */
    void UpdateFingertipTargets(FHandRig& Rig, const FXRHandTrackingState& State);

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
