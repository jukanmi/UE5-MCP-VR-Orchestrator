#include "Core/BP/VRPawn.h"
#include "Core/BP/VRBodyMeasureComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Perception/AISense_Sight.h"

// VR 폰 — 자세 시스템. 키 캘리브레이션, 자세 판정(서기·허리 숙이기·쭈그리기·엎드리기), 몸 메시 배치,
// 자세별 시야 거리 제한, HMD 높이를 따르는 동적 캡슐, 몸 메시 회전·눕힘과 캡슐 수평 맞춤. 나머지 폰 동작은 VRPawn.cpp.

// ============================================================================
// 자세 시스템 — 캘리브레이션 / 자세 판정 / 동적 캡슐
// ============================================================================

void AVRPawn::SetStandingHeight(float Height)
{
    GetWorldTimerManager().ClearTimer(CalibrationSampleTimer);
    GetWorldTimerManager().ClearTimer(CalibrationFinishTimer);
    CalibratedStandingHeight = Height;
    bCalibrated = true;
}

void AVRPawn::StartCalibration()
{
    // 진행 중 타이머가 있으면 정리
    GetWorldTimerManager().ClearTimer(CalibrationSampleTimer);
    GetWorldTimerManager().ClearTimer(CalibrationFinishTimer);

    bCalibrated = false;
    CalibrationAccum = 0.f;
    CalibrationSampleCount = 0;

    // 100ms 간격으로 HMD Z 샘플링
    GetWorldTimerManager().SetTimer(
        CalibrationSampleTimer, this, &AVRPawn::SampleCalibration, 0.1f, true);

    // CalibrationDuration 후 평균 확정
    GetWorldTimerManager().SetTimer(
        CalibrationFinishTimer, this, &AVRPawn::FinishCalibration,
        CalibrationDuration, false);
}

void AVRPawn::SampleCalibration()
{
    const float Z = GetCurrentHMDHeight();
    if (Z > 30.f) // HMD 트래킹이 안 잡힌 0 근처 값 제외
    {
        CalibrationAccum += Z;
        ++CalibrationSampleCount;
    }
}

void AVRPawn::FinishCalibration()
{
    GetWorldTimerManager().ClearTimer(CalibrationSampleTimer);

    // 신체 측정으로 저장한 키가 있으면 평균 대신 그 값을 기준 높이로 확정한다. 확정 시점은 자동 캘리브레이션과 같다 —
    // 대기 시간 동안은 bCalibrated=false 라 HMD 트래킹이 안정되기 전의 값으로 캡슐·자세가 흔들리지 않는다.
    float SavedHeight = 0.f;
    if (BodyMeasure && BodyMeasure->GetSavedHeight(SavedHeight))
    {
        CalibratedStandingHeight = SavedHeight;
        bCalibrated = true;
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] 저장된 신체 측정 키 사용 = %.1f cm"), SavedHeight);
        return;
    }

    if (CalibrationSampleCount > 0)
    {
        CalibratedStandingHeight = CalibrationAccum / CalibrationSampleCount;
        bCalibrated = true;
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] Calibrated standing height = %.1f cm (samples=%d)"),
               CalibratedStandingHeight, CalibrationSampleCount);
    }
    else
    {
        // HMD 트래킹이 전혀 없는 경우(에디터 미연결 등) — 표준값 사용
        CalibratedStandingHeight = 170.f;
        bCalibrated = true;
        UE_LOG(LogTemp, Warning, TEXT("[VRPawn] Calibration fallback to %.1f cm (no valid samples)"),
               CalibratedStandingHeight);
    }
    // 아바타 스케일링 제거 — 항상 네이티브 1:1. CalibratedStandingHeight 는 자세판정(UpdatePosture)용으로만 유지.
}

float AVRPawn::GetCurrentHMDHeight() const
{
    if (!VRCamera) return 0.f;

    // 캡슐 발 기준 절대 높이 = 카메라 월드 Z - 액터 월드 Z + 캡슐 절반 높이.
    // 액터 피벗이 캡슐 정중앙이므로, 발은 액터Z - HalfHeight 에 있다.
    return HeightAboveFloor(VRCamera->GetComponentLocation().Z);
}

float AVRPawn::HeightAboveFloor(float WorldZ) const
{
    return WorldZ - GetActorLocation().Z + InterpedCapsuleHalfHeight;
}

void AVRPawn::UpdatePosture()
{
    if (!bCalibrated || CalibratedStandingHeight <= KINDA_SMALL_NUMBER) return;

    const float Ratio = GetCurrentHMDHeight() / CalibratedStandingHeight;

    // HMD 피치 하향 각도(도, 아래를 볼수록 +) — UE 피치는 위가 +.
    const float PitchDown = VRCamera
        ? static_cast<float>(-FRotator::NormalizeAxis(VRCamera->GetComponentRotation().Pitch))
        : 0.f;

    // 슈미트 트리거 패턴 — 진입/복귀 임계값을 분리해 데드존 확보. 높이(쭈그리기·엎드리기)가 허리 숙이기보다 우선.
    // 허리 숙이기 = 쭈그리기 진입 전 높이대 중 낮은 구간 + 고개 숙임. 서서 고개만 숙이면 비율이 BendRatioMax 이상에 머문다.
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:
        if (Ratio < StandingRatioDown) TransitionTo(EVRPosture::Crouching, Ratio, PitchDown);
        else if (Ratio < BendRatioMax && PitchDown >= BendPitchDownDeg)
            TransitionTo(EVRPosture::Bending, Ratio, PitchDown);
        break;
    case EVRPosture::Bending:
        if (Ratio < StandingRatioDown) TransitionTo(EVRPosture::Crouching, Ratio, PitchDown);
        else if (PitchDown < BendPitchUpDeg || Ratio > BendRatioMaxUp)
            TransitionTo(EVRPosture::Standing, Ratio, PitchDown);
        break;
    case EVRPosture::Crouching:
        // 일어서다 허리 숙이기 조건이 이미 맞으면 Standing 을 거치지 않고 바로 Bending — 한 프레임 서기 깜박임·이벤트 2번 방지.
        if (Ratio > StandingRatioUp)
        {
            const bool bBend = Ratio < BendRatioMax && PitchDown >= BendPitchDownDeg;
            TransitionTo(bBend ? EVRPosture::Bending : EVRPosture::Standing, Ratio, PitchDown);
        }
        else if (Ratio < ProneRatioDown)   TransitionTo(EVRPosture::Prone, Ratio, PitchDown);
        break;
    case EVRPosture::Prone:
        if (Ratio > ProneRatioUp) TransitionTo(EVRPosture::Crouching, Ratio, PitchDown);
        break;
    }
}

void AVRPawn::TransitionTo(EVRPosture NewPosture, float Ratio, float PitchDownDeg)
{
    if (NewPosture == CurrentPosture) return;

    const EVRPosture PrevPosture = CurrentPosture;

    // 이전 자세 태그 회수
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:  RemoveStateTag(TAG_State_Posture_Standing);  break;
    case EVRPosture::Crouching: RemoveStateTag(TAG_State_Posture_Crouching); break;
    case EVRPosture::Prone:     RemoveStateTag(TAG_State_Posture_Prone);     break;
    case EVRPosture::Bending:   RemoveStateTag(TAG_State_Posture_Bending);   break;
    }

    CurrentPosture = NewPosture;

    // 새 자세 태그 부여
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:  AddStateTag(TAG_State_Posture_Standing);  break;
    case EVRPosture::Crouching: AddStateTag(TAG_State_Posture_Crouching); break;
    case EVRPosture::Prone:     AddStateTag(TAG_State_Posture_Prone);     break;
    case EVRPosture::Bending:   AddStateTag(TAG_State_Posture_Bending);   break;
    }

    // 이동속도 재적용 (ApplyMovementSpeed가 CurrentPosture를 참조)
    ApplyMovementSpeed();

    // 판정 튜닝용 — 전이 순간의 높이 비율·피치 하향을 한 줄로 남긴다.
    UE_LOG(LogTemp, Log, TEXT("[VRPawn] Posture %s -> %s (높이비율 %.2f, 피치하향 %.1f도)"),
           *UEnum::GetValueAsString(PrevPosture), *UEnum::GetValueAsString(CurrentPosture),
           Ratio, PitchDownDeg);

    OnPostureChanged.Broadcast(CurrentPosture);
}

void AVRPawn::UpdateBodyPlacement()
{
    USkeletalMeshComponent* Body = GetMesh();
    if (!Body || !VRCamera || Body->IsSimulatingPhysics()) return;   // 래그돌 중엔 물리가 메시를 움직인다

    // 발: 메시는 기본 캡슐 높이 기준으로 붙어 있다. 캡슐이 HMD 높이에 맞춰 줄면(앉기·숙이기) 그만큼 올려 발을 바닥에 둔다 —
    // 안 올리면 몸 전체가 바닥 아래로 가라앉아 어깨·팔꿈치가 실제보다 낮아진다(앉은 자세 실측 약 20cm). 몸 낮추기는 FBIK 가 무릎·허리로.
    Body->SetRelativeLocation(FVector(MeshBaseRelativeLocation.X, MeshBaseRelativeLocation.Y,
                                      MeshBaseRelativeLocation.Z + (BaseCapsuleHalfHeight - InterpedCapsuleHalfHeight)));

    // 수평: 캡슐은 충돌용으로 HMD(눈) 아래를 따르지만, 몸 메시는 목 아래에 둔다(착석 중엔 의자 배치 유지).
    if (SeatedFurniture.IsValid()) return;
    const FTransform Eye = VRCamera->GetComponentTransform();
    const FVector Neck = Eye.GetLocation() + Eye.GetRotation().RotateVector(EyeToNeckOffset);
    const FVector Current = Body->GetComponentLocation();

    // 눕힌 만큼 머리 본이 앞으로 나가므로 발(메시 원점)을 시선 반대로 빼 서 있을 때와 같은 수평 위치에 머리 본을 둔다.
    const float Setback = HeadBoneHeight * Body->GetComponentScale().Z * FMath::Sin(FMath::DegreesToRadians(BodyLeanDeg));
    const FVector Facing = FRotator(0.f, SmoothedBodyYaw, 0.f).Vector();
    Body->SetWorldLocation(FVector(Neck.X - Facing.X * Setback, Neck.Y - Facing.Y * Setback, Current.Z));
}

float AVRPawn::GetStealthSightRange() const
{
    float Factor = 0.f;
    switch (CurrentPosture)
    {
    case EVRPosture::Bending:   Factor = StealthFactorBending;   break;
    case EVRPosture::Crouching: Factor = StealthFactorCrouching; break;
    case EVRPosture::Prone:     Factor = StealthFactorProne;     break;
    case EVRPosture::Standing:  return 0.f;
    }
    return StealthReferenceRange * Factor;
}

UAISense_Sight::EVisibilityResult AVRPawn::CanBeSeenFrom(
    const FCanBeSeenFromContext& Context, FVector& OutSeenLocation,
    int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested,
    float& OutSightStrength, int32* /*UserData*/,
    const FOnPendingVisibilityQueryProcessedDelegate* /*Delegate*/)
{
    OutNumberOfAsyncLosCheckRequested = 0;
    OutSightStrength = 1.f;

    const FVector TargetLoc = GetActorLocation();

    // 자세별 시야 거리 제한 — 서기(0)는 제한 없이 관찰자 SightRadius 에 맡긴다.
    const float Range = GetStealthSightRange();
    if (Range > 0.f && FVector::DistSquared(Context.ObserverLocation, TargetLoc) > FMath::Square(Range))
    {
        return UAISense_Sight::EVisibilityResult::NotVisible;
    }

    // 기본 AI 시선 검사와 동일 — 관찰자 시점에서 플레이어까지 가시성 채널 직선이 막히지 않아야 보인다.
    UWorld* World = GetWorld();
    if (!World) return UAISense_Sight::EVisibilityResult::NotVisible;

    FHitResult Hit;
    const FCollisionQueryParams Params(SCENE_QUERY_STAT(AILineOfSight), true, Context.IgnoreActor);
    const bool bHit = World->LineTraceSingleByChannel(Hit, Context.ObserverLocation, TargetLoc, ECC_Visibility, Params);
    ++OutNumberOfLoSChecksPerformed;

    // 엔진 기본 판정과 동일 — 플레이어 본인뿐 아니라 플레이어 소유 액터(들고 있는 아이템 등)에 먼저 맞아도 보인 것으로 친다.
    const AActor* HitActor = Hit.GetActor();
    if (!bHit || (HitActor && HitActor->IsOwnedBy(this)))
    {
        OutSeenLocation = TargetLoc;
        return UAISense_Sight::EVisibilityResult::Visible;
    }
    return UAISense_Sight::EVisibilityResult::NotVisible;
}

void AVRPawn::UpdateDynamicCapsule(float DeltaTime)
{
    if (!bCalibrated) return;

    UCapsuleComponent* Cap = GetCapsuleComponent();
    if (!Cap || !VROrigin) return;

    // 타겟 절반 높이 = HMD가 바닥에서 얼마나 떠 있는지의 절반.
    // HMD가 머리 꼭대기보다 약간 아래(눈높이)인 점은 사용자별 편차로 묻고 비율로 흡수.
    const float TargetHalfHeight = FMath::Clamp(
        GetCurrentHMDHeight() * 0.5f, MinCapsuleHalfHeight, BaseCapsuleHalfHeight);

    // VInterp 스무딩 — 프레임 드랍·콜리전 업데이트 비동기로 인한 jitter 방지
    InterpedCapsuleHalfHeight = FMath::FInterpTo(
        InterpedCapsuleHalfHeight, TargetHalfHeight, DeltaTime, HeightInterpSpeed);

    // 캡슐 적용 — sweep=true 로 천장 침투 방지
    Cap->SetCapsuleHalfHeight(InterpedCapsuleHalfHeight, true);

    // Rising Floor 역보정 — CMC가 캡슐 바닥을 바닥에 붙이므로 ActorZ = InterpedHalfHeight.
    // 카메라 월드 Z 가 HMD가 보고하는 바닥 기준 절대 높이와 일치하려면
    // VROrigin Z = -InterpedHalfHeight 이어야 한다. 즉 BeginPlay의 -BaseHalfHeight
    // 식을 동적값으로 확장한 형태.
    const FVector OriginLoc = VROrigin->GetRelativeLocation();
    VROrigin->SetRelativeLocation(FVector(OriginLoc.X, OriginLoc.Y,
                                          -InterpedCapsuleHalfHeight + CameraHeightOffset));
}

// ----------------------------------------------------------------------------
// 몸 맞추기 — 몸 메시 회전·눕힘, 캡슐 수평 위치를 HMD 에 맞춘다
// ----------------------------------------------------------------------------

void AVRPawn::UpdateBodyRotation(float DeltaTime)
{
    if (!VRCamera || !GetMesh()) return;

    // 착석 중에는 의자 방향을 유지하고 고개만 회전. 눕혀 있었다면 수직으로 되돌린다.
    if (SeatedFurniture.IsValid())
    {
        if (BodyLeanDeg > KINDA_SMALL_NUMBER)
        {
            // 가구가 액터 yaw 를 돌려 놓았을 수 있으니 낡은 시선 yaw 가 아니라 액터 yaw 기준으로 세운다.
            BodyLeanDeg = 0.f;
            SmoothedBodyYaw = GetActorRotation().Yaw;
            GetMesh()->SetWorldRotation(FRotator(0.f, SmoothedBodyYaw + BodyMeshYawOffset, 0.f));
        }
        return;
    }

    // HMD(헤드셋)가 바라보는 수평 월드 각도
    const float CameraYaw = VRCamera->GetComponentRotation().Yaw;

    if (BodyRotationInterpSpeed > 0.f && DeltaTime > KINDA_SMALL_NUMBER && bBodyYawInit)
    {
        SmoothedBodyYaw = FMath::RInterpTo(FRotator(0.f, SmoothedBodyYaw, 0.f), FRotator(0.f, CameraYaw, 0.f),
                                           DeltaTime, BodyRotationInterpSpeed).Yaw;
    }
    else
    {
        SmoothedBodyYaw = CameraYaw;
    }
    bBodyYawInit = true;

    // 눕힘 — HMD 가 키 대비 낮아진 비율에 비례해 앞으로 눕힌다(상한 BodyLeanMaxDeg).
    // 기하(cos θ = 높이 ÷ 접힘 한계)로 풀면 머리 본이 15cm 가까이 높게 남았다: 눕히면 다리가 펴져 몸이 길어지고,
    // 안 눕힌 구간은 FBIK 가 몸을 못 접는다. PIE 에서 비율 0.9~0.35 를 재서 머리 본 오차가 작은 기울기로 맞췄다.
    float TargetLean = 0.f;
    if (bCalibrated && CalibratedStandingHeight > KINDA_SMALL_NUMBER)
    {
        const float Ratio = GetCurrentHMDHeight() / CalibratedStandingHeight;
        TargetLean = FMath::Clamp((1.f - Ratio) * BodyLeanDegPerRatio, 0.f, BodyLeanMaxDeg);
    }
    BodyLeanDeg = DeltaTime > KINDA_SMALL_NUMBER
        ? FMath::FInterpTo(BodyLeanDeg, TargetLean, DeltaTime, HeightInterpSpeed)
        : TargetLean;

    // 회전 = 시선 yaw → 앞으로 눕힘(UE 피치 음수 = 앞으로 숙임) → 메시 정면 보정.
    const FQuat Facing = FRotator(0.f, SmoothedBodyYaw, 0.f).Quaternion();
    const FQuat Lean = FRotator(-BodyLeanDeg, 0.f, 0.f).Quaternion();
    const FQuat MeshFix = FRotator(0.f, BodyMeshYawOffset, 0.f).Quaternion();
    GetMesh()->SetWorldRotation(Facing * Lean * MeshFix);
}

void AVRPawn::SyncCapsuleToHMD()
{
    // 표준 VR 패턴: 캡슐(액터)을 HMD의 월드 XY 위치 아래로 따라가게 하되,
    // 같은 양을 VROrigin에서 빼서 HMD의 월드 위치는 그대로 유지한다.
    //
    // WHY 월드 위치 차이 기반: 카메라·캡슐의 실제 월드 좌표 차이만큼만 이동시키면
    // 그 차이가 매 프레임 0으로 수렴한다. 과거 구현은 HMD의 절대 트래킹 좌표
    // (VRCamera 상대 위치)를 매 프레임 VROrigin에 누적시켜, VROrigin이 액터
    // 피벗에서 점점 표류 → 스냅턴 회전 시 그 누적 오프셋이 큰 호를 그리며
    // 텔레포트되는 버그가 있었다.
    if (!VRCamera || !VROrigin) return;

    const FVector CapsuleWorld = GetActorLocation();
    const FVector CameraWorld  = VRCamera->GetComponentLocation();
    const FVector OffsetXY(CameraWorld.X - CapsuleWorld.X,
                           CameraWorld.Y - CapsuleWorld.Y, 0.f);
    if (OffsetXY.IsNearlyZero()) return;

    // 캡슐을 HMD 아래로 이동 (sweep: 벽 통과 방지). 실제 이동량만큼만 VROrigin을
    // 역보정해 — 벽에 막혀 캡슐이 덜 움직였으면 카메라도 그만큼만 따라간다.
    const FVector Before = GetActorLocation();
    AddActorWorldOffset(OffsetXY, true);
    const FVector Applied = GetActorLocation() - Before;
    VROrigin->AddWorldOffset(-Applied);
}
