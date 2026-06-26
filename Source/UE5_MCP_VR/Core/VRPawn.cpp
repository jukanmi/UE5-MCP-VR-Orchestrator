#include "VRPawn.h"
#include "GameplayTagUtils.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "KineticProjectile.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "NativeGameplayTags.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Engine/OverlapResult.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/ForceFeedbackEffect.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "DrawDebugHelpers.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IMotionController.h"
#include "../NPC/SmartNPC.h"
#include "../NPC/NPCManager.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "VoiceInputComponent.h"
#include "../NPC/Struct/NPCActionKeys.h"
#include "../Inventory/InventoryComponent.h"
#include "../UI/PlayerHUDWidget.h"
#include "Blueprint/UserWidget.h"

// ============================================================================
// 생성자
// ============================================================================

AVRPawn::AVRPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    // VROrigin: 모든 VR 컴포넌트의 공통 부모 — 캡슐 루트와 분리하여 트래킹 오프셋 적용
    VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
    VROrigin->SetupAttachment(RootComponent);

    // HMD 카메라 — XR 런타임이 위치·회전을 매 프레임 갱신
    VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
    VRCamera->SetupAttachment(VROrigin);

    // 왼손 모션컨트롤러 — Grip 포즈 (손 메시 부착용)
    MotionControllerLeft = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerLeft"));
    MotionControllerLeft->SetupAttachment(VROrigin);
    MotionControllerLeft->MotionSource = IMotionController::LeftHandSourceId;

    // 왼손 Aim 포즈 — 조준·포인터·UI 인터랙션용. 현재 사용처는 없지만 미리
    // 책정해두어 후속 기능에서 바로 쓸 수 있게 한다.
    MotionControllerLeftAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerLeftAim"));
    MotionControllerLeftAim->SetupAttachment(VROrigin);
    MotionControllerLeftAim->MotionSource = FName("LeftAim");

    // 오른손 모션컨트롤러 — Grip 포즈 (손 메시 부착용)
    MotionControllerRight = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerRight"));
    MotionControllerRight->SetupAttachment(VROrigin);
    MotionControllerRight->MotionSource = IMotionController::RightHandSourceId;

    // 오른손 Aim 포즈 — 조준·발사용. OpenXR이 컨트롤러별로 별도 제공하는 포즈로
    // 자연스러운 조준 축과 정렬되어 있다. Grip 포즈와는 ~30° 기울어져 있어
    // 라인트레이스에는 반드시 Aim을 써야 한다.
    MotionControllerRightAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerRightAim"));
    MotionControllerRightAim->SetupAttachment(VROrigin);
    MotionControllerRightAim->MotionSource = FName("RightAim");

    // 손 메시 제거됨 — 풀바디 FBIK(ABP_VRPawn) 손이 컨트롤러를 향해 역산되므로
    // 별도 손 메시는 중복. 무기·아이템은 X_Bot hand 본 소켓(GetMesh())에 부착.

    // §4 동역학 근접 — 손 위치 시각 마커. **실제 타격 판정은 Tick 의 능동 스피어 쿼리(TryMeleeHits).**
    // 본 소켓에 붙인 패시브 overlap 은 애니 본에서 overlap 이벤트 누락이 잦아 쓰지 않음(콜리전 OFF).
    MeleeSphereLeft = CreateDefaultSubobject<USphereComponent>(TEXT("MeleeSphereLeft"));
    MeleeSphereLeft->SetupAttachment(GetMesh(), TEXT("LeftHand"));  // Mixamo X_Bot 본 이름
    MeleeSphereLeft->InitSphereRadius(MeleeSphereRadius);
    MeleeSphereLeft->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    MeleeSphereRight = CreateDefaultSubobject<USphereComponent>(TEXT("MeleeSphereRight"));
    MeleeSphereRight->SetupAttachment(GetMesh(), TEXT("RightHand"));  // Mixamo X_Bot 본 이름
    MeleeSphereRight->InitSphereRadius(MeleeSphereRadius);
    MeleeSphereRight->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // AI 퍼셉션 소스 등록
    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
    StimuliSource->RegisterWithPerceptionSystem();

    // 음성 입력 컴포넌트
    VoiceInput = CreateDefaultSubobject<UVoiceInputComponent>(TEXT("VoiceInput"));

    // 인벤토리 컴포넌트
    Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));

    // VR에서는 컨트롤러 회전이 캐릭터 회전에 직접 반영되지 않도록 설정
    bUseControllerRotationYaw  = false;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll  = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;

    // 동적 캡슐 리사이즈의 상한·역보정 기준이 될 기본 절반 높이 캐싱
    if (UCapsuleComponent* Cap = GetCapsuleComponent())
    {
        BaseCapsuleHalfHeight = Cap->GetUnscaledCapsuleHalfHeight();
        InterpedCapsuleHalfHeight = BaseCapsuleHalfHeight;
    }
}

// ============================================================================
// BeginPlay
// ============================================================================

void AVRPawn::BeginPlay()
{
    Super::BeginPlay();

    // 음성 입력 — 대상/플레이어 공급자 + transcript 콜백 바인딩
    if (VoiceInput)
    {
        VoiceInput->ResolveTargetNpc = [this]()
        {
            if (CurrentTargetNPCID.IsEmpty()) DetectNearbyNPC();
            return CurrentTargetNPCID;
        };
        VoiceInput->ResolvePlayerId = [this]() { return GetName(); };
        VoiceInput->OnTranscriptReady.BindUObject(this, &AVRPawn::HandleVoiceTranscript);
    }

    // HMD 트래킹 원점을 바닥(Floor)으로 설정 — Quest 룸스케일 기준
    // Stage = 바닥 기준 룸스케일 트래킹 (UE5.5에서 Floor 대체)
    UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Stage);

    // VROrigin은 캡슐 루트(= 바닥 +절반높이)에 붙어 있다. Stage 트래킹은 HMD
    // 높이를 "바닥" 기준으로 보고하므로, VROrigin을 캡슐 절반 높이만큼 내려
    // 트래킹 공간 원점을 실제 바닥에 맞춘다. 이를 빼지 않으면 카메라가
    // 캡슐 절반 높이(기본 88cm)만큼 떠 보인다.
    if (VROrigin && GetCapsuleComponent())
    {
        const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        VROrigin->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
    }

    // Enhanced Input IMC 등록
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Sub =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (VRMappingContext)
                Sub->AddMappingContext(VRMappingContext, 0);
        }
    }

    RefreshStats();
    AddStateTag(TAG_State_Idle);

    // 초기 자세는 Standing 으로 가정하고 태그만 미리 부여. 캘리브레이션이 끝나면
    // UpdatePosture()가 실시간 Z 비율로 다시 확정한다.
    AddStateTag(TAG_State_Posture_Standing);

    // 사용자 키 캘리브레이션 시작 — HMD 트래킹이 안정화되는 시간을 잠시 두고
    StartCalibration();

    // HUD 생성 — 로컬 플레이어 컨트롤러일 때만
    if (HUDWidgetClass)
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (PC->IsLocalController())
            {
                HUDWidget = CreateWidget<UPlayerHUDWidget>(PC, HUDWidgetClass);
                if (HUDWidget)
                {
                    HUDWidget->AddToViewport();
                }
            }
        }
    }
}

// ============================================================================
// Tick
// ============================================================================

void AVRPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    SyncCapsuleToHMD();
    UpdateSmoothTurn(DeltaTime);
    UpdatePosture();
    UpdateDynamicCapsule(DeltaTime);

    // §4 근접 — 손(Grip 컨트롤러) 속도 추적. ½mv² 의 v. 컨트롤러는 kinematic 이라
    // GetVelocity()=0 → 위치 델타/dt 수동 산출. EMA 로 트래킹 스파이크 평탄화.
    if (DeltaTime > KINDA_SMALL_NUMBER && MotionControllerLeft && MotionControllerRight)
    {
        const FVector CurL = MotionControllerLeft->GetComponentLocation();
        const FVector CurR = MotionControllerRight->GetComponentLocation();
        if (bHandVelInit)
        {
            // 텔레포트·트래킹 튐 방지 — 속도 >9000cm/s(90m/s, 인간 스윙 ~10m/s 불가)는 글리치로 보고
            // 0 처리. ½mv² 데미지 폭주·물리 폭발 차단. 속도(cm/s) 기준이라 프레임레이트 독립(저FPS 정상스윙 오판 X).
            const FVector VelL = (CurL - PrevHandLocLeft) / DeltaTime;
            const FVector VelR = (CurR - PrevHandLocRight) / DeltaTime;
            const FVector RawL = (VelL.Size() > 9000.f) ? FVector::ZeroVector : VelL;
            const FVector RawR = (VelR.Size() > 9000.f) ? FVector::ZeroVector : VelR;
            HandVelLeft  = FMath::Lerp(HandVelLeft,  RawL, HandVelSmoothing);
            HandVelRight = FMath::Lerp(HandVelRight, RawR, HandVelSmoothing);

            // 디버그 — 손 위치 구체 + 실시간 속도. 근접 미작동 단계 진단(속도 0? 쿼리 미스?).
            if (bDebugMelee)
            {
                DrawDebugSphere(GetWorld(), CurL, MeleeSphereRadius, 12, FColor::Cyan,   false, 0.f);
                DrawDebugSphere(GetWorld(), CurR, MeleeSphereRadius, 12, FColor::Yellow, false, 0.f);
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(8801, 0.f, FColor::Yellow,
                        FString::Printf(TEXT("[Melee] L=%.2f  R=%.2f m/s  (min=%.2f)"),
                            HandVelLeft.Size() / 100.f, HandVelRight.Size() / 100.f, MinImpactSpeed));
                }
            }

            // 근접 타격 — 컨트롤러 위치에서 능동 스피어 오버랩(본 부착 패시브 overlap 회피).
            TryMeleeHits(CurR, HandVelRight, /*bRightHand=*/true);
            TryMeleeHits(CurL, HandVelLeft,  /*bRightHand=*/false);
        }
        PrevHandLocLeft  = CurL;
        PrevHandLocRight = CurR;
        bHandVelInit = true;
    }
}

// ============================================================================
// 자세 시스템 — 캘리브레이션 / 자세 판정 / 동적 캡슐
// ============================================================================

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
    return VRCamera->GetComponentLocation().Z - GetActorLocation().Z + InterpedCapsuleHalfHeight;
}

// ----------------------------------------------------------------------------
// FBIK Effector — 컴포넌트(메시) 공간 변환
// 컨트롤러/HMD는 VROrigin 프레임, 메시는 캡슐 프레임이라 서로 다른 World 위치에 있다.
// Control Rig는 메시 컴포넌트 공간에서 풀므로, 각 World Transform 을 메시 World 기준
// 상대 변환(GetRelativeTransform)으로 바꿔 넘긴다. 머리·양손 모두 동일 기준(메시).
// ----------------------------------------------------------------------------
FTransform AVRPawn::GetHeadEffectorCS() const
{
    if (!VRCamera || !GetMesh()) return FTransform::Identity;
    FTransform T = VRCamera->GetComponentTransform().GetRelativeTransform(GetMesh()->GetComponentTransform());
    // 머리 본 축 보정을 로컬 공간에 적용(우측 곱).
    T.SetRotation(T.GetRotation() * HeadEffectorOffset.Quaternion());
    return T;
}

FTransform AVRPawn::GetLeftHandEffectorCS() const
{
    if (!MotionControllerLeft || !GetMesh()) return FTransform::Identity;
    FTransform T = MotionControllerLeft->GetComponentTransform().GetRelativeTransform(GetMesh()->GetComponentTransform());
    // 그립 축 보정을 손 로컬 공간에 적용(우측 곱) — 손이 회전해도 보정이 따라감.
    T.SetRotation(T.GetRotation() * LeftHandGripOffset.Quaternion());
    return T;
}

FTransform AVRPawn::GetRightHandEffectorCS() const
{
    if (!MotionControllerRight || !GetMesh()) return FTransform::Identity;
    FTransform T = MotionControllerRight->GetComponentTransform().GetRelativeTransform(GetMesh()->GetComponentTransform());
    T.SetRotation(T.GetRotation() * RightHandGripOffset.Quaternion());
    return T;
}

void AVRPawn::UpdatePosture()
{
    if (!bCalibrated || CalibratedStandingHeight <= KINDA_SMALL_NUMBER) return;

    const float Ratio = GetCurrentHMDHeight() / CalibratedStandingHeight;

    // 슈미트 트리거 패턴 — 진입/복귀 임계값을 분리해 데드존 확보
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:
        if (Ratio < StandingRatioDown) TransitionTo(EVRPosture::Crouching);
        break;
    case EVRPosture::Crouching:
        if (Ratio > StandingRatioUp)       TransitionTo(EVRPosture::Standing);
        else if (Ratio < ProneRatioDown)   TransitionTo(EVRPosture::Prone);
        break;
    case EVRPosture::Prone:
        if (Ratio > ProneRatioUp) TransitionTo(EVRPosture::Crouching);
        break;
    }
}

void AVRPawn::TransitionTo(EVRPosture NewPosture)
{
    if (NewPosture == CurrentPosture) return;

    // 이전 자세 태그 회수
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:  RemoveStateTag(TAG_State_Posture_Standing);  break;
    case EVRPosture::Crouching: RemoveStateTag(TAG_State_Posture_Crouching); break;
    case EVRPosture::Prone:     RemoveStateTag(TAG_State_Posture_Prone);     break;
    }

    CurrentPosture = NewPosture;

    // 새 자세 태그 부여
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:  AddStateTag(TAG_State_Posture_Standing);  break;
    case EVRPosture::Crouching: AddStateTag(TAG_State_Posture_Crouching); break;
    case EVRPosture::Prone:     AddStateTag(TAG_State_Posture_Prone);     break;
    }

    // 이동속도 재적용 (ApplyMovementSpeed가 CurrentPosture를 참조)
    ApplyMovementSpeed();

    UE_LOG(LogTemp, Verbose, TEXT("[VRPawn] Posture -> %s"),
           *UEnum::GetValueAsString(CurrentPosture));

    OnPostureChanged.Broadcast(CurrentPosture);
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

// ============================================================================
// 입력 바인딩
// ============================================================================

void AVRPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (IA_Move)          EIC->BindAction(IA_Move,          ETriggerEvent::Triggered, this, &AVRPawn::OnMove);
        if (IA_SnapTurn)
        {
            // 부드러운 연속 회전 — Triggered로 입력값 갱신, Completed/Canceled에서 0 리셋.
            EIC->BindAction(IA_SnapTurn, ETriggerEvent::Triggered, this, &AVRPawn::OnTurn);
            EIC->BindAction(IA_SnapTurn, ETriggerEvent::Completed, this, &AVRPawn::OnTurnReleased);
            EIC->BindAction(IA_SnapTurn, ETriggerEvent::Canceled,  this, &AVRPawn::OnTurnReleased);
        }
        if (IA_Attack)        EIC->BindAction(IA_Attack,        ETriggerEvent::Started,   this, &AVRPawn::OnAttack);
        if (IA_Interact)      EIC->BindAction(IA_Interact,      ETriggerEvent::Started,   this, &AVRPawn::OnInteract);
        if (IA_VoiceInput)
        {
            // Push-to-talk: 누름 시작 → 녹음, 뗌 → 종료
            EIC->BindAction(IA_VoiceInput, ETriggerEvent::Started,   this, &AVRPawn::OnVoiceStart);
            EIC->BindAction(IA_VoiceInput, ETriggerEvent::Completed, this, &AVRPawn::OnVoiceStop);
            EIC->BindAction(IA_VoiceInput, ETriggerEvent::Canceled,  this, &AVRPawn::OnVoiceStop);
        }
    }
}

// ============================================================================
// 로코모션
// ============================================================================

void AVRPawn::OnMove(const FInputActionValue& Value)
{
    FVector2D Input = Value.Get<FVector2D>();
    if (Input.IsNearlyZero()) return;

    // HMD Yaw 기준으로 이동 방향 계산 (컨트롤러 회전이 아닌 시선 방향)
    const FRotator CameraYaw(0.f, VRCamera->GetComponentRotation().Yaw, 0.f);
    const FVector Forward = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
    const FVector Right   = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);

    AddMovementInput(Forward, Input.Y);
    AddMovementInput(Right,   Input.X);

    RemoveStateTag(TAG_State_Idle);
    AddStateTag(TAG_State_Action_Common_Move);
}

void AVRPawn::OnTurn(const FInputActionValue& Value)
{
    // 입력값만 저장 — 실제 회전은 Tick(UpdateSmoothTurn)에서 프레임 보정 적용.
    TurnAxisInput = Value.Get<FVector2D>().X;
}

void AVRPawn::OnTurnReleased(const FInputActionValue& Value)
{
    TurnAxisInput = 0.f;
}

void AVRPawn::UpdateSmoothTurn(float DeltaTime)
{
    // 조이스틱 X 입력만큼 프레임당 연속 회전. 데드존 미만은 무시.
    if (!VRCamera || FMath::Abs(TurnAxisInput) < TurnInputDeadzone) return;

    const float TurnDelta = TurnAxisInput * SmoothTurnRate * DeltaTime;

    // HMD 월드 위치를 피벗으로 회전 — 액터 피벗 기준으로 돌면 HMD가 호를 그리며
    // 측면으로 밀리므로, 회전 전후 HMD 월드 XY 차이만큼 역보정해 제자리 회전 유지.
    const FVector PivotBefore = VRCamera->GetComponentLocation();
    AddActorWorldRotation(FRotator(0.f, TurnDelta, 0.f));
    const FVector PivotAfter = VRCamera->GetComponentLocation();
    AddActorWorldOffset(FVector(PivotBefore.X - PivotAfter.X,
                                PivotBefore.Y - PivotAfter.Y, 0.f));
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

// ============================================================================
// 전투
// ============================================================================

void AVRPawn::OnAttack(const FInputActionValue& /*Value*/)
{
    RemoveStateTag(TAG_State_Idle);
    AddStateTag(TAG_State_Action_Combat_Attack);

    // 공격 소음 발생 (NPC 청각 감지용)
    UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), 1.f, this, 0.f, NPCActionKeys::NoiseTag_Attack);

    // §4 원거리 — 히트스캔 폐기, 투사체 발사. 오른손 Aim 포즈(조준 정렬) 기준 전방.
    // 명중·데미지는 투사체의 ½mv²(KineticProjectile::OnHit)가 처리. 근접은 스윙 overlap.
    if (ProjectileClass && MotionControllerRightAim)
    {
        const FVector  SpawnLoc = MotionControllerRightAim->GetComponentLocation();
        const FRotator SpawnRot = MotionControllerRightAim->GetComponentRotation();

        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;
        SpawnParams.Instigator = this;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        if (AKineticProjectile* Proj = GetWorld()->SpawnActor<AKineticProjectile>(ProjectileClass, SpawnLoc, SpawnRot, SpawnParams))
        {
            // 근접과 동일한 J→HP 환산·상한 주입 — 전투 일관성.
            Proj->InitProjectile(this, KineticDamageScale, MaxKineticDamage);
        }

        // 던진 손(오른손) 럼블.
        PlayHitHaptic(/*bRightHand=*/true);
    }

    // 던지는 모션.
    if (AttackMontage)
    {
        if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
        {
            Anim->Montage_Play(AttackMontage);
            FOnMontageEnded EndDelegate;
            EndDelegate.BindUObject(this, &AVRPawn::OnAttackMontageEnded);
            Anim->Montage_SetEndDelegate(EndDelegate, AttackMontage);
            return;
        }
    }
    RemoveStateTag(TAG_State_Action_Combat_Attack);
    AddStateTag(TAG_State_Idle);
}

void AVRPawn::OnAttackMontageEnded(UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
{
    RemoveStateTag(TAG_State_Action_Combat_Attack);
    AddStateTag(TAG_State_Idle);
}

void AVRPawn::PlayHitHaptic(bool bRightHand)
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;

    // [B] VR 모션 컨트롤러 햅틱 — 명중 손. 에셋 미할당 시 no-op.
    if (HitHapticEffect)
    {
        PC->PlayHapticEffect(HitHapticEffect, bRightHand ? EControllerHand::Right : EControllerHand::Left,
            HitHapticScale, /*bLoop=*/false);
    }
    // [A] 게임패드 진동 모터 폴백.
    if (HitForceFeedbackEffect)
    {
        FForceFeedbackParameters FFParams;
        FFParams.bLooping = false;
        PC->ClientPlayForceFeedback(HitForceFeedbackEffect, FFParams);
    }
}

void AVRPawn::TryMeleeHits(const FVector& HandLoc, const FVector& HandVel, bool bRightHand)
{
    // 2단 임계 — bPush(밀치기) 이상이면 밀고, bStrike(데미지) 이상이면 공격(TakeDamage→SmartNPC 공격 인지).
    // 가벼운 밀침(bPush~bStrike 사이)은 데미지 없음 = LLM 이 공격으로 안 봄.
    const float SpeedMs = HandVel.Size() / 100.f;          // cm/s → m/s
    const bool  bPush   = SpeedMs >= MinImpactSpeed;
    const bool  bStrike = SpeedMs >= MeleeStrikeSpeed;
    if (!bPush && !bDebugMelee) return;

    // 손 위치에서 능동 스피어 오버랩(Pawn 채널) — 패시브 overlap 의 본부착 불안정 회피.
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeHit), /*bTraceComplex=*/false, this);
    // Pawn(서 있는 NPC 캡슐) + PhysicsBody(넉다운 래그돌 메시) 둘 다 — 쓰러진 NPC 저글 타격 가능(§5.C.6).
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_Pawn);
    ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    const bool bAnyOverlap = GetWorld()->OverlapMultiByObjectType(Overlaps, HandLoc, FQuat::Identity,
        ObjParams, FCollisionShape::MakeSphere(MeleeSphereRadius), Params);

    if (bDebugMelee && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(bRightHand ? 8811 : 8812, 0.f,
            bAnyOverlap ? FColor::Green : FColor::Silver,
            FString::Printf(TEXT("[Melee %s] overlaps=%d push=%d strike=%d"),
                bRightHand ? TEXT("R") : TEXT("L"), Overlaps.Num(), bPush ? 1 : 0, bStrike ? 1 : 0));
    }

    if (!bAnyOverlap || !bPush) return;   // 최소 밀치기 임계 + overlap.

    const float Now = GetWorld()->GetTimeSeconds();

    // 만료된 약참조 키 정리 — 소멸한 NPC 의 TWeakObjectPtr 엔트리는 자동 제거 안 돼 누적되므로.
    for (auto It = LastMeleeHitTime.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid()) { It.RemoveCurrent(); }
    }

    const float Energy = 0.5f * WeaponMass * SpeedMs * SpeedMs;                // ½mv² (J)
    const float Damage = FMath::Clamp(Energy * KineticDamageScale, 0.f, MaxKineticDamage);

    for (const FOverlapResult& O : Overlaps)
    {
        ASmartNPC* NPC = Cast<ASmartNPC>(O.GetActor());
        if (!NPC) continue;

        // 같은 NPC 재타격 쿨다운 — 매 틱 쿼리라 쿨다운 없으면 연속 타격 폭주.
        if (const float* Last = LastMeleeHitTime.Find(NPC))
        {
            if (Now - *Last < MeleeHitCooldown) continue;
        }
        LastMeleeHitTime.Add(NPC, Now);

        FName HitBone = NAME_None;

        // 강타(bStrike) → 데미지. SmartNPC::TakeDamage 가 인지 이벤트(공격)를 발생시킴.
        // 가벼운 밀침(bStrike 미만)은 TakeDamage 를 안 불러 NPC 가 공격으로 인지하지 않음.
        if (bStrike)
        {
            // 부위 인지 best-effort — 손 위치에서 NPC 메시 최근접 본. 실패 시 None→Torso 폴백.
            if (USkeletalMeshComponent* NpcMesh = NPC->GetMesh())
            {
                HitBone = NpcMesh->FindClosestBone(HandLoc);
            }

            // FPointDamageEvent 로 보내야 SmartNPC 가 BoneName(부위 배율)·ShotDirection(래그돌 임펄스) 처리.
            FPointDamageEvent Ev;
            Ev.HitInfo.BoneName    = HitBone;
            Ev.HitInfo.ImpactPoint = HandLoc;
            Ev.ShotDirection       = HandVel.GetSafeNormal();
            Ev.DamageTypeClass     = UDamageType::StaticClass();
            NPC->TakeDamage(Damage, Ev, GetController(), this);

            PlayHitHaptic(bRightHand);
        }

        // 밀치기 — 가벼운 접촉도 밀되 공격 인지는 없음. 죽었으면 HandleDeath 의 래그돌 임펄스가 처리.
        if (!NPC->bIsDead && KnockbackScale > 0.f)
        {
            const float PushSpeed = FMath::Min(SpeedMs * KnockbackScale, MaxKnockbackSpeed);
            const FVector PushVel = HandVel.GetSafeNormal() * PushSpeed;
            NPC->LaunchCharacter(PushVel, /*bXYOverride=*/true, /*bZOverride=*/false);
        }

        if (bDebugMelee && GEngine)
        {
            DrawDebugSphere(GetWorld(), HandLoc, MeleeSphereRadius, 12,
                bStrike ? FColor::Red : FColor::Orange, false, 1.f);
            GEngine->AddOnScreenDebugMessage(-1, 2.f, bStrike ? FColor::Red : FColor::Orange,
                FString::Printf(TEXT("[Melee] %s %s  dmg=%.1f"),
                    bStrike ? TEXT("STRIKE") : TEXT("PUSH"), *NPC->GetName(), bStrike ? Damage : 0.f));
        }
    }
}

// ============================================================================
// NPC 상호작용
// ============================================================================

void AVRPawn::OnInteract(const FInputActionValue& /*Value*/)
{
    DetectNearbyNPC();
}

void AVRPawn::DetectNearbyNPC()
{
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    GetWorld()->OverlapMultiByObjectType(
        Overlaps, GetActorLocation(), FQuat::Identity,
        FCollisionObjectQueryParams(ECollisionChannel::ECC_Pawn),
        FCollisionShape::MakeSphere(500.f), Params);

    FString FoundID;
    float MinDistSq = FLT_MAX;
    for (const FOverlapResult& R : Overlaps)
    {
        if (ASmartNPC* NPC = Cast<ASmartNPC>(R.GetActor()))
        {
            float D = FVector::DistSquared(GetActorLocation(), NPC->GetActorLocation());
            if (D < MinDistSq) { MinDistSq = D; FoundID = NPC->AgentID; }
        }
    }

    // 미발견 시 기존 타겟 유지 — 빈 값 덮어쓰기로 유효 대상이 소실되는 것 방지.
    if (!FoundID.IsEmpty())
    {
        CurrentTargetNPCID = FoundID;
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] NPC 발견: %s"), *FoundID);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] 주변 NPC 없음 (기존 타겟 유지: %s)"), *CurrentTargetNPCID);
    }
}

void AVRPawn::SendNPCDialogue(const FString& Text)
{
    if (CurrentTargetNPCID.IsEmpty())
    {
        DetectNearbyNPC();
    }
    if (CurrentTargetNPCID.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRPawn] SendNPCDialogue 실패 — 대상 NPC 없음"));
        return;
    }

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            // player_id = actor 이름 — affinity DB 키와 일치
            Manager->SendPlayerDialogue(GetName(), CurrentTargetNPCID, Text);
        }
    }
}

void AVRPawn::OnVoiceStart(const FInputActionValue& Value)
{
    if (VoiceInput) VoiceInput->StartTalking();
}

void AVRPawn::OnVoiceStop(const FInputActionValue& Value)
{
    if (VoiceInput) VoiceInput->StopTalking();
}

void AVRPawn::HandleVoiceTranscript(const FString& PlayerId, const FString& TargetNpc, const FString& Transcript)
{
    // ASR transcript → 기존 단순 대화 경로 재사용. 대상은 ASR 가 echo 한 값 우선,
    // 없으면 현재 타겟. 스텁 transcript("[ASR stub] …")도 그대로 흘려보내 end-to-end 검증.
    const FString Target = TargetNpc.IsEmpty() ? CurrentTargetNPCID : TargetNpc;
    if (Target.IsEmpty() || Transcript.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRPawn] Voice transcript 폐기 — target/transcript 비어있음"));
        return;
    }
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->SendPlayerDialogue(PlayerId.IsEmpty() ? GetName() : PlayerId, Target, Transcript);
        }
    }
}

void AVRPawn::LogIKMetrics()
{
    USkeletalMeshComponent* M = GetMesh();
    if (!M || !MotionControllerRight) return;

    // X_Bot 본 이름(접두어 없음). 어깨(상완 시작)→손 = 아바타 오른팔 길이.
    const FVector Shoulder = M->GetSocketLocation(TEXT("RightArm"));
    const FVector Hand     = M->GetSocketLocation(TEXT("RightHand"));
    const float ArmLen     = (Hand - Shoulder).Size();

    // 컨트롤러(=실제 손 타겟)가 아바타 어깨에서 떨어진 거리 = 필요한 도달거리.
    const FVector Ctrl  = MotionControllerRight->GetComponentLocation();
    const float Reach   = (Ctrl - Shoulder).Size();
    const float Scale   = M->GetRelativeScale3D().X;

    const FString Msg = FString::Printf(
        TEXT("[IK] ArmLen=%.1f  Reach=%.1f  diff=%.1f  scale=%.3f  (Reach>ArmLen=팔짧음, Reach<<ArmLen=팔길어 팔꿈치접힘)"),
        ArmLen, Reach, Reach - ArmLen, Scale);
    UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);

    // 현재 이펙터 타겟(메시 공간) — CR 변수 Default Value 에 박아 프리뷰 재현용.
    auto Dump = [](const TCHAR* Name, const FTransform& T)
    {
        const FVector L = T.GetLocation();
        const FRotator R = T.Rotator();
        UE_LOG(LogTemp, Warning,
            TEXT("[IK] %s  Loc=(%.2f, %.2f, %.2f)  Rot=(P=%.2f, Y=%.2f, R=%.2f)"),
            Name, L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll);
    };
    Dump(TEXT("HeadTarget     "), GetHeadEffectorCS());
    Dump(TEXT("LeftHandTarget "), GetLeftHandEffectorCS());
    Dump(TEXT("RightHandTarget"), GetRightHandEffectorCS());

#if !UE_BUILD_SHIPPING
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, Msg);
#endif
}

// ============================================================================
// 스탯
// ============================================================================

void AVRPawn::ApplyMovementSpeed()
{
    UCharacterMovementComponent* MC = GetCharacterMovement();
    if (!MC) return;

    const float Base = CurrentStats.Movement.WalkSpeed;

    // 자세별 속도 클램프 — Standing 100% / Crouching = CrouchSpeed / Prone = 20%
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:
        MC->MaxWalkSpeed = Base;
        break;
    case EVRPosture::Crouching:
        MC->MaxWalkSpeed = CurrentStats.Movement.CrouchSpeed;
        break;
    case EVRPosture::Prone:
        MC->MaxWalkSpeed = Base * 0.2f;
        break;
    }
    MC->MaxWalkSpeedCrouched = CurrentStats.Movement.CrouchSpeed;
}

void AVRPawn::RefreshStats()
{
    CurrentStats.RecalculateCombatStats();
    ApplyMovementSpeed();
}

float AVRPawn::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
                          AController* EventInstigator, AActor* DamageCauser)
{
    float Actual = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    CurrentStats.Resources.Health = FMath::Max(0.f, CurrentStats.Resources.Health - Actual);

    if (CurrentStats.Resources.Health <= 0.f)
    {
        RemoveStateTag(TAG_State_Idle);
        AddStateTag(TAG_State_Condition_Dead);
        HandleDeath();
    }
    return Actual;
}

// ============================================================================
// 체크포인트
// ============================================================================

void AVRPawn::SaveCheckpoint(const FVector& Location, const FRotator& Rotation)
{
    CheckpointLocation = Location;
    CheckpointRotation = Rotation;
    CheckpointHP       = CurrentStats.Resources.Health;
    bHasCheckpoint     = true;
}

// ============================================================================
// 사망 / 리스폰
// ============================================================================

void AVRPawn::HandleDeath()
{
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
        DisableInput(PC);

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetVisibility(false);

    GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &AVRPawn::Respawn, RespawnDelay, false);
}

void AVRPawn::Respawn()
{
    if (bHasCheckpoint)
    {
        SetActorLocationAndRotation(CheckpointLocation, CheckpointRotation);
        CurrentStats.Resources.Health = CheckpointHP;
    }
    else
    {
        if (AActor* Start = UGameplayStatics::GetActorOfClass(GetWorld(), APlayerStart::StaticClass()))
            SetActorLocationAndRotation(Start->GetActorLocation(), Start->GetActorRotation());
        CurrentStats.Resources.Health = CurrentStats.Resources.MaxHealth;
    }

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetMesh()->SetVisibility(true);
    RemoveStateTag(TAG_State_Condition_Dead);
    AddStateTag(TAG_State_Idle);

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
        EnableInput(PC);
}

// ============================================================================
// 게임플레이 태그
// ============================================================================

void AVRPawn::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    TagContainer = GameplayTags;
}

void AVRPawn::AddStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::AddState(GameplayTags, Tag);
}

void AVRPawn::RemoveStateTag(FGameplayTag Tag)
{
    GameplayTagUtils::RemoveState(GameplayTags, Tag);
}
