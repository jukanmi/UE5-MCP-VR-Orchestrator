#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "VRPostureComponent.generated.h"

class AVRPawn;

/** VR 자세 — HMD 높이 비율(서기·쭈그리기·엎드리기) + HMD 피치와 높이 구간(허리 숙이기)으로 판정.
 *  AnimBP/FBIK가 이 값으로 스테이트·이동속도를 결정. 기존 값 번호 유지를 위해 Bending 은 끝에 추가. */
UENUM(BlueprintType)
enum class EVRPosture : uint8
{
    Standing  UMETA(DisplayName = "Standing"),
    Crouching UMETA(DisplayName = "Crouching"),
    Prone     UMETA(DisplayName = "Prone"),
    Bending   UMETA(DisplayName = "Bending")
};

/** 자세 전이 알림용 델리게이트 — AnimBP·UI 등이 폴링 대신 이 이벤트로 반응. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVRPostureChanged, EVRPosture, NewPosture);

/**
 * VR 폰의 자세 시스템 — 키 캘리브레이션, 자세 판정, HMD 높이를 따르는 동적 캡슐, 몸 메시 회전·눕힘·배치.
 * 폰의 위치·카메라·몸 메시를 읽고 움직이므로 AVRPawn 에만 붙는다. 시야 거리 제한(CanBeSeenFrom)은 폰이 이 컴포넌트의
 * 자세별 계수(GetStealthSightRange)를 읽어 처리한다.
 *
 * 매 틱 폰이 Update 를 부른다(컴포넌트 자체 틱 없음): 몸 회전·눕힘 → 자세 판정 → 동적 캡슐 → 몸 배치.
 */
UCLASS(ClassGroup = (VR))
class UE5_MCP_VR_API UVRPostureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVRPostureComponent();

    // ── 자세 상태 ──

    /** 현재 자세. AnimBP가 Property Access로 워커 스레드에서 읽음. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Posture")
    EVRPosture CurrentPosture = EVRPosture::Standing;

    /** 캘리브레이션된 사용자 정자세 HMD 높이(cm). 0이면 미보정 상태. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Posture")
    float CalibratedStandingHeight = 0.f;

    /** 캘리브레이션 완료 여부. UpdatePosture()는 이 플래그가 true여야 작동. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Posture")
    bool bCalibrated = false;

    /** 자세 전이 시 브로드캐스트. AnimBP / UI 가 바인딩. */
    UPROPERTY(BlueprintAssignable, Category = "VR|Posture")
    FOnVRPostureChanged OnPostureChanged;

    // ── 자세 판정 튜닝 ──

    /** Stand → Crouch 진입 비율 (H_max 대비). 기본 0.75 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.5", ClampMax = "0.95"))
    float StandingRatioDown = 0.75f;

    /** Crouch → Stand 복귀 비율. StandingRatioDown 보다 커야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.5", ClampMax = "0.95"))
    float StandingRatioUp = 0.80f;

    /** Crouch → Prone 진입 비율. 기본 0.40 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.2", ClampMax = "0.6"))
    float ProneRatioDown = 0.40f;

    /** Prone → Crouch 복귀 비율. ProneRatioDown 보다 커야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.2", ClampMax = "0.6"))
    float ProneRatioUp = 0.45f;

    /** Standing → Bending 진입 HMD 피치 하향 각도(도). 이 각도 이상 숙이고 높이도 허리 숙이기 구간이어야 진입. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "10.0", ClampMax = "80.0"))
    float BendPitchDownDeg = 30.f;

    /** Bending → Standing 복귀 피치 하향 각도(도). BendPitchDownDeg 보다 작아야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "5.0", ClampMax = "70.0"))
    float BendPitchUpDeg = 20.f;

    /** Standing → Bending 진입 높이 비율 상한. 쭈그리기 진입(StandingRatioDown) 이상 ~ 이 값 미만에서만 허리 숙이기.
     *  서서 고개만 숙이면 비율이 거의 안 내려가 이 값 이상에 머물러 Standing 으로 남는다.
     *  StandingRatioDown < BendRatioMax < BendRatioMaxUp 이어야 함. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.8", ClampMax = "1.0"))
    float BendRatioMax = 0.88f;

    /** Bending → Standing 복귀 높이 비율. BendRatioMax 보다 커야 데드존이 생긴다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.8", ClampMax = "1.0"))
    float BendRatioMaxUp = 0.91f;

    /** 허리 숙이기 걷기 속도 = WalkSpeed × 이 값. 서기(1.0)와 쭈그리기(CrouchSpeed/WalkSpeed) 사이. 전력질주 중엔 적용 안 함. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.2", ClampMax = "1.0"))
    float BendSpeedRatio = 0.75f;

    // ── 캡슐·캘리브레이션 튜닝 ──

    /** 캡슐 절반 높이 최소값(cm) — 포복 시 적용. 엎드리기 캡슐은 눕히지 않고 이 높이의 수직 캡슐로 둔다
     *  (캡슐이 HMD 높이를 따라 연속으로 줄어들어 바닥·천장 스윕으로 침투를 막는다). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "10.0", ClampMax = "60.0"))
    float MinCapsuleHalfHeight = 22.f;

    /** 캡슐·VR Origin Z 보간 속도. 높을수록 빠르게 따라감. 권장값 8 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "1.0", ClampMax = "20.0"))
    float HeightInterpSpeed = 8.f;

    /** 캘리브레이션 샘플링 지속시간(초). BeginPlay 직후 이 시간동안 HMD Z 평균. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "0.5", ClampMax = "5.0"))
    float CalibrationDuration = 2.f;

    /** 시야 높이 미세 오프셋(cm). VROrigin Z에 더함. **기본 0 — 비워둘 것.**
     *  ⚠️ 0이 아니면 카메라 월드Z가 시프트되고, GetCurrentHMDHeight()가 그 카메라Z를
     *  역산하므로 보고 높이가 오염→캡슐(아바타 몸)이 오프셋만큼 짧아짐→카메라가 짧은 몸
     *  위로 떠 '시야 너무 높음'. 과거 -30 이 정확히 이 버그를 유발(아바타 머리 뜸을
     *  카메라 침몰로 가리려다 역효과). 머리 본이 뜨면 여기 말고 HeadEffectorOffset/
     *  FBIK head effector 에서 교정할 것. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Posture", meta = (ClampMin = "-20.0", ClampMax = "20.0"))
    float CameraHeightOffset = 0.f;

    // ── 은신 — 자세별 시야 거리 ──

    /** 은신 기준 시야 거리(cm) — 자세 계수를 곱해 적·NPC 가 이 자세의 플레이어를 보는 최대 거리를 정한다.
     *  서 있을 땐 제한 없음(관찰자 자신의 SightRadius 만 적용). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Stealth", meta = (ClampMin = "100.0"))
    float StealthReferenceRange = 3000.f;

    /** 허리 숙이기 시야 거리 계수. 서기(제한 없음) > 허리 숙이기 > 쭈그리기 > 엎드리기 순이어야 한다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Stealth", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float StealthFactorBending = 0.8f;

    /** 쭈그리기 시야 거리 계수. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Stealth", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float StealthFactorCrouching = 0.6f;

    /** 엎드리기 시야 거리 계수. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Stealth", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float StealthFactorProne = 0.35f;

    // ── 몸 메시 ──

    /** 눈(HMD) → 목 위치 오프셋(HMD 로컬, cm). 몸 메시를 눈이 아니라 이 목 위치 아래에 놓는다 —
     *  고개를 숙이거나 돌리면 눈은 앞으로 나가지만 몸통은 제자리라, 눈 아래에 두면 몸 전체가 끌려간다. PIE 중 즉시 반영. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body")
    FVector EyeToNeckOffset = FVector(-10.f, 0.f, -20.f);

    /** 메시 정면 보정 오프셋 (Mixamo X_Bot 등 표준 스켈레탈 메시는 -90도 회전 시 캐릭터 전방 정렬). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body")
    float BodyMeshYawOffset = -90.f;

    /** 몸(메시) 회전 추종 보간 속도 (0이면 HMD 시선과 즉시 1:1 동기화, >0이면 부드럽게 추종). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body", meta = (ClampMin = "0.0"))
    float BodyRotationInterpSpeed = 0.f;

    /** 키 대비 HMD 높이가 1 줄어들 때마다 눕히는 각도(도). 0.1 낮아지면 이 값의 10% 만큼 앞으로 눕는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body", meta = (ClampMin = "0.0", ClampMax = "200.0"))
    float BodyLeanDegPerRatio = 110.f;

    /** 몸 눕힘 상한(도). 90 이면 완전히 수평 — 엎드려도 상체를 든 모양이 되도록 그보다 작게 둔다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Body", meta = (ClampMin = "0.0", ClampMax = "90.0"))
    float BodyLeanMaxDeg = 68.f;

    /** 현재 몸 눕힘 각도(도, 0 = 수직). 캡슐 보간과 같은 속도(HeightInterpSpeed)로 목표를 따라간다. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "VR|Body")
    float BodyLeanDeg = 0.f;

    // ── 폰이 부르는 것 ──

    /** 이 높이(cm) 이하의 HMD 값은 트래킹이 안 잡힌 것(0 근처)으로 본다 — 캘리브레이션·신체 측정이 같이 쓴다. */
    static constexpr float MinTrackedHMDHeight = 30.f;

    /** 폰 생성자에서 한 번 — 기본 캡슐 절반 높이를 기억한다. 동적 리사이즈의 상한·역보정 기준. */
    void InitCapsuleHeight(float HalfHeight);

    /** BeginPlay — 몸 메시의 기준 위치와 머리 본 높이를 레퍼런스 포즈에서 읽는다. */
    void InitBodyMeshReference();

    /** 매 틱 — 몸 회전·눕힘 → 자세 판정 → 동적 캡슐 → 몸 배치 순서. */
    void Update(float DeltaTime);

    /** 외부 트리거(예: 양손 그립 동시 입력)로 캘리브레이션 재시작. */
    UFUNCTION(BlueprintCallable, Category = "VR|Posture")
    void StartCalibration();

    /** 자세 판정 기준 높이를 확정한다(자동 샘플링 중단 + bCalibrated). 저장된 신체 측정값 적용·측정 직후 갱신 공통 경로. */
    void SetStandingHeight(float Height);

    /** 현재 HMD가 바닥(=캡슐 발) 기준으로 얼마나 높이 있는지(cm). */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VR|Posture")
    float GetCurrentHMDHeight() const;

    /** 월드 Z 를 바닥(=캡슐 발) 기준 높이(cm)로 바꾼다. GetCurrentHMDHeight 와 같은 좌표계 — 신체 측정이 컨트롤러 높이를 잴 때 쓴다. */
    float HeightAboveFloor(float WorldZ) const;

    /** 현재 자세에서 적·NPC 가 플레이어를 볼 수 있는 최대 거리(cm). 0 이하면 제한 없음(서기). */
    UFUNCTION(BlueprintPure, Category = "VR|Stealth")
    float GetStealthSightRange() const;

private:
    AVRPawn* GetPawn() const;

    /** 캘리브레이션 샘플 1회 (타이머에서 호출) */
    void SampleCalibration();

    /** 캘리브레이션 종료 — 평균값 확정 + bCalibrated=true */
    void FinishCalibration();

    /** 매 틱 — HMD Z 비율로 자세 전이 판정 (히스테리시스) */
    void UpdatePosture();

    /** 자세 전이 — Enum/태그 갱신 + 이동속도 적용 + OnPostureChanged 브로드캐스트 */
    void TransitionTo(EVRPosture NewPosture, float Ratio, float PitchDownDeg);

    /** 매 틱 — 동적 캡슐 리사이즈 + Rising Floor 역보정 (VInterp 스무딩) */
    void UpdateDynamicCapsule(float DeltaTime);

    /** 매 틱 — 몸 메시를 발은 바닥에, 수평은 목 아래에 둔다. 캡슐이 줄어도 메시가 바닥 아래로 가라앉지 않게. */
    void UpdateBodyPlacement();

    /** 매 틱 — 몸(스켈레탈 메시)이 HMD 시선 Yaw 를 바라보게 하고, 낮아진 만큼 앞으로 눕힌다. */
    void UpdateBodyRotation(float DeltaTime);

    /** 생성자에서 기억한 기본 캡슐 절반 높이. 동적 리사이즈의 상한·역보정 기준. */
    float BaseCapsuleHalfHeight = 88.f;

    /** 현재 보간된 캡슐 절반 높이 — Tick에서 타겟값으로 InterpTo */
    float InterpedCapsuleHalfHeight = 88.f;

    /** 캘리브레이션 샘플링 누적값·횟수 */
    float CalibrationAccum = 0.f;
    int32 CalibrationSampleCount = 0;
    FTimerHandle CalibrationSampleTimer;
    FTimerHandle CalibrationFinishTimer;

    /** BeginPlay 시점 메시 상대 위치(기본 캡슐 높이 기준). 캡슐 높이 변화만큼 Z 를 보정한다. */
    FVector MeshBaseRelativeLocation = FVector::ZeroVector;

    /** 몸 눕힘을 뺀 순수 몸 방향 yaw(= 시선 yaw 추종값). 눕힌 메시 회전에서 yaw 를 역산하면 틀어지므로 따로 들고 있다. */
    float SmoothedBodyYaw = 0.f;
    bool bBodyYawInit = false;

    /** 메시 원점(발)에서 머리 본까지 높이(cm) — BeginPlay 에 레퍼런스 포즈에서 읽는다. */
    float HeadBoneHeight = 130.f;
};
