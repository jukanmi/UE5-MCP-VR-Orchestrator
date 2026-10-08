#include "Core/BP/VRPawn.h"
#include "Core/BP/VRHandComponent.h"
#include "Core/BP/VRPlayerUIComponent.h"
#include "Core/BP/VRBodyMeasureComponent.h"
#include "Core/Debug/VRCheatManager.h"
#include "Core/BP/VRMeleeComponent.h"
#include "Core/Utils/GameplayTagUtils.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Core/BP/KineticProjectile.h"
#include "Core/Physics/KineticDamage.h"
#include "Core/Types/CollisionChannels.h"
#include "EngineUtils.h"
#include "Core/Utils/PlayerInteractionUtils.h"
#include "Core/Utils/PawnDeathUtils.h"
#include "Furniture/Subsystems/FurnitureManager.h"
#include "Furniture/BP/FurnitureActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "NativeGameplayTags.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Engine/OverlapResult.h"
#include "Engine/DamageEvents.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IMotionController.h"
#include "IXRTrackingSystem.h"
#include "NPC/BP/SmartNPC.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Utils/DiceSystem.h"
#include "Villager/VillagerCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "NPC/Struct/NPCActionKeys.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Subsystems/ItemManager.h"
#include "UI/Trade/TradeSessionActor.h"
#include "Villager/MerchantStall.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

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

    // 동역학 근접 — 손 위치 시각 마커. **실제 타격 판정은 UVRMeleeComponent 의 능동 스피어 쿼리.**
    // 본 소켓에 붙인 패시브 overlap 은 애니 본에서 overlap 이벤트 누락이 잦아 쓰지 않음(콜리전 OFF).
    MeleeCombat = CreateDefaultSubobject<UVRMeleeComponent>(TEXT("MeleeCombat"));

    MeleeSphereLeft = CreateDefaultSubobject<USphereComponent>(TEXT("MeleeSphereLeft"));
    MeleeSphereLeft->SetupAttachment(GetMesh(), TEXT("LeftHand"));  // Mixamo X_Bot 본 이름
    MeleeSphereLeft->InitSphereRadius(MeleeCombat->MeleeSphereRadius);
    MeleeSphereLeft->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    MeleeSphereRight = CreateDefaultSubobject<USphereComponent>(TEXT("MeleeSphereRight"));
    MeleeSphereRight->SetupAttachment(GetMesh(), TEXT("RightHand"));  // Mixamo X_Bot 본 이름
    MeleeSphereRight->InitSphereRadius(MeleeCombat->MeleeSphereRadius);
    MeleeSphereRight->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // 손 — 컨트롤러 Grip 포즈에 붙은 앵커. 물리 손바닥·손가락·제약은 BeginPlay 에 손 컴포넌트가 만든다.
    // 그립 보정 = X_Bot 손 본 축과 컨트롤러 그립 축 차이(좌우 미러).
    HandLeft = CreateDefaultSubobject<UVRHandComponent>(TEXT("HandLeft"));
    HandLeft->SetupAttachment(MotionControllerLeft);
    HandLeft->Hand = EControllerHand::Left;
    HandLeft->GripOffset = FRotator(180.f, 0.f, 90.f);
    HandRight = CreateDefaultSubobject<UVRHandComponent>(TEXT("HandRight"));
    HandRight->SetupAttachment(MotionControllerRight);
    HandRight->Hand = EControllerHand::Right;
    HandRight->GripOffset = FRotator(0.f, 0.f, -90.f);

    PlayerUI = CreateDefaultSubobject<UVRPlayerUIComponent>(TEXT("PlayerUI"));
    BodyMeasure = CreateDefaultSubobject<UVRBodyMeasureComponent>(TEXT("BodyMeasure"));

    // AI 퍼셉션 소스 등록
    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
    StimuliSource->RegisterWithPerceptionSystem();

    // 인벤토리 컴포넌트 (시작 골드는 컴포넌트 기본값 150 — BP_VRPawn 에서 덮어쓸 수 있다)
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

#if !UE_BUILD_SHIPPING
        // 디버그·튜닝 콘솔 명령(TuneGrab·DumpInventoryHUD·ToggleInventory·Cheat_Unequip)은 치트 매니저에 있다.
        // 프로젝트에 게임모드·컨트롤러 BP 가 없어 에셋으로 CheatClass 를 못 지정하므로 여기서 지정한다.
        // 에디터는 기본 치트 매니저를 이미 만들어 두므로(AddCheats 는 있으면 건너뛴다) 우리 클래스가 아니면 바꿔 끼운다.
        PC->CheatClass = UVRCheatManager::StaticClass();
        if (!PC->CheatManager || !PC->CheatManager->IsA<UVRCheatManager>())
        {
            PC->CheatManager = NewObject<UCheatManager>(PC, PC->CheatClass);
            PC->CheatManager->InitCheatManager();
        }
#endif
    }

    RefreshStats();
    AddStateTag(TAG_State_Idle);

    // 초기 자세는 Standing 으로 가정하고 태그만 미리 부여. 캘리브레이션이 끝나면
    // UpdatePosture()가 실시간 Z 비율로 다시 확정한다.
    AddStateTag(TAG_State_Posture_Standing);

    // 사용자 키 캘리브레이션 시작 — HMD 트래킹이 안정화되는 시간을 잠시 두고
    StartCalibration();

    // UI — 포인터 비주얼, HUD·채팅 위젯 생성, 인벤토리 닫힌 상태로 시작.
    if (PlayerUI) PlayerUI->Init();

    if (GetMesh()) MeshBaseRelativeLocation = GetMesh()->GetRelativeLocation();

    if (HandLeft)  HandLeft->InitPhysics();
    if (HandRight) HandRight->InitPhysics();

    // 몸 메시 애니메이션은 물리 뒤에 — 손 이펙터가 물리 손바닥 위치를 읽는데, 물리 전(기본값)에 돌면 지난 프레임 위치로
    // 손을 그려 같은 프레임 물리로 움직인 쥔 물건보다 한 프레임 뒤처진다(이동 중 3m/s 면 약 3cm 어긋남).
    // 폰 틱(손끝 모양 배치)은 애님이 채운 손끝 목표를 한 프레임 늦게 읽게 되지만, 목표가 손바닥 기준 상대값이라 영향이 작다.
    if (GetMesh()) GetMesh()->SetTickGroup(TG_PostPhysics);

    // 손이 NPC(Pawn)를 막게 됐으니 내 캡슐·몸 메시는 손 채널을 무시해 몸에 손이 걸리지 않게 한다.
    // 한쪽만 무시해도 충돌 응답은 둘 중 약한 쪽을 따른다. BP 프로필이 덮어쓸 수 있어 생성자 대신 여기서 설정.
    for (UPrimitiveComponent* Own : { static_cast<UPrimitiveComponent*>(GetCapsuleComponent()), static_cast<UPrimitiveComponent*>(GetMesh()) })
    {
        if (!Own) continue;
        Own->SetCollisionResponseToChannel(ECC_HandLeft, ECR_Ignore);
        Own->SetCollisionResponseToChannel(ECC_HandRight, ECR_Ignore);
    }

#if !UE_BUILD_SHIPPING
    // show 는 켜고 끄는 토글이고 PIE 마다 새 뷰포트라 꺼진 채 시작한다 — 한 번 보내면 켜진다.
    if (bShowCollisionOnStart && GetWorld() && GetWorld()->GetGameViewport())
    {
        GetWorld()->GetGameViewport()->ConsoleCommand(TEXT("show collision"));
    }
#endif
}

// ============================================================================
// 손 — 물리·판정은 UVRHandComponent, 폰은 잡기 입력으로 합쳐 넘긴다
// ============================================================================

void AVRPawn::UpdateHands(float DeltaTime)
{
    // 순서: 두 손 트래킹·드라이브 → 제스처 → 접촉 쥐기 → 쥐기 제약 동기화.
    const TStaticArray<UVRHandComponent*, 2> Hands{ HandLeft, HandRight };
    for (UVRHandComponent* H : Hands)
    {
        if (H) H->UpdateTracking(DeltaTime);
    }
    for (UVRHandComponent* H : Hands)
    {
        if (H && H->UpdateGesture()) UpdateGrabInput(H->IsLeft());
    }
    for (UVRHandComponent* H : Hands)
    {
        if (!H) continue;
        // 인벤토리가 열렸거나 그 손에 이미 쥔 게 있으면 새로 쥐지 않는다(접촉 쥐기 유지·놓기는 계속 본다).
        const bool bAllowNew = !IsInventoryOpen() && Inventory && !Inventory->GetHeldItem(H->GetHandSlot());
        if (H->UpdateContactGrab(bAllowNew)) UpdateGrabInput(H->IsLeft(), H->IsContactHeld() ? H->GetContactItem() : nullptr);
    }
    for (UVRHandComponent* H : Hands)
    {
        if (H) H->SyncGrab();
    }
}

const FXRHandTrackingState& AVRPawn::GetHandTrackState(EControllerHand Hand) const
{
    static const FXRHandTrackingState Invalid;
    const UVRHandComponent* H = GetHand(Hand);
    return H ? H->GetTrackState() : Invalid;
}

// ============================================================================
// Tick
// ============================================================================

void AVRPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    SyncCapsuleToHMD();
    UpdateSmoothTurn(DeltaTime);
    UpdateBodyRotation(DeltaTime);
    UpdatePosture();
    UpdateDynamicCapsule(DeltaTime);
    UpdateBodyPlacement();
    if (PlayerUI) PlayerUI->Update(DeltaTime);
    UpdateStamina(DeltaTime);
    UpdateDash(DeltaTime);
    UpdateHands(DeltaTime);

    // 동역학 근접 — 손 갱신 뒤 실제 손 속도를 재고 그 손으로 친다.
    if (MeleeCombat) MeleeCombat->Update(DeltaTime);
}

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

// ----------------------------------------------------------------------------
// FBIK Effector — 컴포넌트(메시) 공간 변환
// 컨트롤러/HMD는 VROrigin 프레임, 메시는 캡슐 프레임이라 서로 다른 World 위치에 있다.
// Control Rig는 메시 컴포넌트 공간에서 풀므로, 각 World Transform 을 메시 World 기준
// 상대 변환(GetRelativeTransform)으로 바꿔 넘긴다. 머리·양손 모두 동일 기준(메시).
// ----------------------------------------------------------------------------
FTransform AVRPawn::GetHeadEffectorCS() const
{
    if (!VRCamera || !GetMesh()) return FTransform::Identity;
    // 위치는 눈이 아니라 머리 본 자리(눈 뒤·아래) — 눈 위치를 넘기면 FBIK 가 척추를 앞으로 당겨 몸이 쏠린다.
    FTransform Head = VRCamera->GetComponentTransform();
    Head.AddToTranslation(Head.GetRotation().RotateVector(EyeToHeadOffset));
    FTransform T = Head.GetRelativeTransform(GetMesh()->GetComponentTransform());
    // 머리 본 축 보정을 로컬 공간에 적용(우측 곱).
    T.SetRotation(T.GetRotation() * HeadEffectorOffset.Quaternion());
    return T;
}

FTransform AVRPawn::GetLeftHandEffectorCS() const
{
    return HandLeft ? HandLeft->GetEffectorCS(GetMesh()) : FTransform::Identity;
}

FTransform AVRPawn::GetRightHandEffectorCS() const
{
    return HandRight ? HandRight->GetEffectorCS(GetMesh()) : FTransform::Identity;
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
        if (Ratio > StandingRatioUp)       TransitionTo(EVRPosture::Standing, Ratio, PitchDown);
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
    Body->SetWorldLocation(FVector(Neck.X, Neck.Y, Current.Z));
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

// ============================================================================
// 입력 바인딩
// ============================================================================

void AVRPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // 채팅 열기 — 키보드 전용이라 IA 에셋 없이 레거시 키 바인딩. Enhanced 와 병행 동작한다.
    PlayerInputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AVRPawn::OnChatKey);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (IA_Move)
        {
            EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AVRPawn::OnMove);
            // Triggered 는 입력이 0이 되면 안 오므로, Sprint 해제는 Completed/Canceled 에서.
            EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &AVRPawn::OnMoveReleased);
            EIC->BindAction(IA_Move, ETriggerEvent::Canceled,  this, &AVRPawn::OnMoveReleased);
        }
        if (IA_SnapTurn)
        {
            // 부드러운 연속 회전 — Triggered로 입력값 갱신, Completed/Canceled에서 0 리셋.
            EIC->BindAction(IA_SnapTurn, ETriggerEvent::Triggered, this, &AVRPawn::OnTurn);
            EIC->BindAction(IA_SnapTurn, ETriggerEvent::Completed, this, &AVRPawn::OnTurnReleased);
            EIC->BindAction(IA_SnapTurn, ETriggerEvent::Canceled,  this, &AVRPawn::OnTurnReleased);
        }
        if (IA_Attack)
        {
            EIC->BindAction(IA_Attack, ETriggerEvent::Started,   this, &AVRPawn::OnAttack);
            // 인벤토리 열림 중에는 같은 트리거가 UI 클릭이 된다 — 뗌도 받아야 포인터가 눌린 채 남지 않는다.
            EIC->BindAction(IA_Attack, ETriggerEvent::Completed, this, &AVRPawn::OnAttackReleased);
            EIC->BindAction(IA_Attack, ETriggerEvent::Canceled,  this, &AVRPawn::OnAttackReleased);
        }
        if (IA_Interact)      EIC->BindAction(IA_Interact,      ETriggerEvent::Started,   this, &AVRPawn::OnInteract);
        if (IA_InventoryToggle) EIC->BindAction(IA_InventoryToggle, ETriggerEvent::Started, this, &AVRPawn::OnInventoryToggle);
        if (IA_MenuToggle)      EIC->BindAction(IA_MenuToggle,      ETriggerEvent::Started, this, &AVRPawn::OnMenuToggle);
        if (IA_BodyMeasureCapture) EIC->BindAction(IA_BodyMeasureCapture, ETriggerEvent::Started, this, &AVRPawn::OnBodyMeasureCapture);
        else UE_LOG(LogTemp, Warning, TEXT("[VRPawn] IA_BodyMeasureCapture 가 비어 있어 신체 측정 단계가 넘어가지 않는다 — BP_VRPawn 에 지정하라"));
        if (IA_Dash)            EIC->BindAction(IA_Dash,            ETriggerEvent::Started, this, &AVRPawn::OnDash);
        if (IA_Grab)
        {
            EIC->BindAction(IA_Grab, ETriggerEvent::Started,   this, &AVRPawn::OnGrabStartRight);
            EIC->BindAction(IA_Grab, ETriggerEvent::Triggered, this, &AVRPawn::OnGrabValueRight);
            // 그립을 뗀 순간이 곧 던지는 순간 — Completed 뿐 아니라 Canceled 도 받아야
            // 트래킹이 끊기며 취소된 경우에 아이템이 손에 영구히 붙어 남지 않는다.
            EIC->BindAction(IA_Grab, ETriggerEvent::Completed, this, &AVRPawn::OnGrabReleaseRight);
            EIC->BindAction(IA_Grab, ETriggerEvent::Canceled,  this, &AVRPawn::OnGrabReleaseRight);
        }
        if (IA_GrabLeft)
        {
            EIC->BindAction(IA_GrabLeft, ETriggerEvent::Started,   this, &AVRPawn::OnGrabStartLeft);
            EIC->BindAction(IA_GrabLeft, ETriggerEvent::Triggered, this, &AVRPawn::OnGrabValueLeft);
            EIC->BindAction(IA_GrabLeft, ETriggerEvent::Completed, this, &AVRPawn::OnGrabReleaseLeft);
            EIC->BindAction(IA_GrabLeft, ETriggerEvent::Canceled,  this, &AVRPawn::OnGrabReleaseLeft);
        }
    }
}

// ============================================================================
// 로코모션
// ============================================================================

void AVRPawn::OnMove(const FInputActionValue& Value)
{
    // 메뉴가 열린 동안 이동 입력은 무시 — 월드가 계속 흐르므로 서 있는 채로 메뉴를 본다.
    if (IsUIBlockingInput())
    {
        SetSprinting(false);
        LastMoveInput = FVector2D::ZeroVector;
        return;
    }

    // 착석 중 스틱 이동 잠금 — 기상은 Interact 재입력(토글)만.
    if (SeatedFurniture.IsValid())
    {
        SetSprinting(false);
        LastMoveInput = FVector2D::ZeroVector;
        return;
    }

    FVector2D Input = Value.Get<FVector2D>();
    if (Input.IsNearlyZero())
    {
        SetSprinting(false);
        StopMoveState();
        LastMoveInput = FVector2D::ZeroVector;
        return;
    }

    // 대쉬 방향 산출용 — OnDash 는 입력 이벤트가 따로 와서 스틱 값을 직접 볼 수 없다.
    LastMoveInput = Input;

    // 스틱을 끝까지 밀면 달리기 — 별도 입력 액션 없이 magnitude 로만 판정.
    SetSprinting(Input.Size() > SprintThreshold);

    // HMD Yaw 기준으로 이동 방향 계산 (컨트롤러 회전이 아닌 시선 방향)
    const FRotator CameraYaw(0.f, VRCamera->GetComponentRotation().Yaw, 0.f);
    const FVector Forward = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
    const FVector Right   = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);

    AddMovementInput(Forward, Input.Y);
    AddMovementInput(Right,   Input.X);

    RemoveStateTag(TAG_State_Idle);
    AddStateTag(TAG_State_Action_Common_Move);
}

void AVRPawn::OnMoveReleased(const FInputActionValue& /*Value*/)
{
    SetSprinting(false);
    StopMoveState();
    LastMoveInput = FVector2D::ZeroVector;
}

// 스틱을 놓거나 입력이 0 이 되는 경로가 둘이라 태그 회수를 한 곳에 모은다.
// 회수하지 않으면 한 번 움직인 뒤 영구히 Move 상태로 남아 AI 인지·StateTree 분기가 어긋난다.
void AVRPawn::StopMoveState()
{
    RemoveStateTag(TAG_State_Action_Common_Move);
    AddStateTag(TAG_State_Idle);
}

void AVRPawn::SetSprinting(bool bNewSprinting)
{
    // 스태미나 고갈 중에는 진입 요청을 무시한다. 해제는 회복이 SprintUnlockStaminaRatio 를
    // 넘을 때(UpdateStamina) 이뤄지므로, 그 전까지 스틱을 끝까지 밀어도 Walk 로 남는다.
    if (bNewSprinting && bStaminaExhausted) return;

    if (bIsSprinting == bNewSprinting) return;

    bIsSprinting = bNewSprinting;
    // Crouching/Prone 은 ApplyMovementSpeed 가 자세 분기에서 자체 속도를 쓰므로 Sprint 가 자동 억제된다.
    ApplyMovementSpeed();
}

void AVRPawn::UpdateStamina(float DeltaTime)
{
    FGameResources& Res = CurrentStats.Resources;

    // 소모 조건 셋 — Sprint 중 + 선 자세 + 실제로 이동 중.
    //  · 자세: Crouching/Prone 은 ApplyMovementSpeed 가 Sprint 속도를 안 쓰므로 소모도 없어야 한다.
    //  · 실제 이동: 벽에 막혀 제자리인데 스틱만 최대로 밀고 있을 때 스태미나가 마르는 건 부자연스럽다.
    const bool bConsuming = bIsSprinting
        && (CurrentPosture == EVRPosture::Standing || CurrentPosture == EVRPosture::Bending)
        && GetVelocity().SizeSquared2D() > KINDA_SMALL_NUMBER;

    if (bConsuming)
    {
        TimeSinceSprintStopped = 0.f;
        Res.Stamina = FMath::Max(0.f, Res.Stamina - SprintStaminaCostPerSec * DeltaTime);

        if (Res.Stamina <= 0.f)
        {
            bStaminaExhausted = true;
            SetSprinting(false);   // ApplyMovementSpeed() 가 Walk 로 되돌린다
        }
        return;
    }

    // 회복 — 중단 직후 즉시 차오르면 끊어 달리기로 무한 Sprint 가 되므로 지연을 둔다.
    TimeSinceSprintStopped += DeltaTime;
    if (TimeSinceSprintStopped < StaminaRegenDelaySec) return;

    Res.Stamina = FMath::Min(Res.MaxStamina, Res.Stamina + Res.StaminaRegen * DeltaTime);

    if (bStaminaExhausted && Res.Stamina >= Res.MaxStamina * SprintUnlockStaminaRatio)
    {
        bStaminaExhausted = false;
    }
}

void AVRPawn::OnDash(const FInputActionValue& /*Value*/)
{
    if (SeatedFurniture.IsValid() || IsUIBlockingInput()) return;

    // 자세 제한은 Sprint 와 동일 기준 — 웅크리거나 엎드린 채로 튀어 나가지 않는다.
    if (CurrentPosture != EVRPosture::Standing && CurrentPosture != EVRPosture::Bending) return;

    UWorld* World = GetWorld();
    UCharacterMovementComponent* MC = GetCharacterMovement();
    if (!World || !MC || !VRCamera) return;

    const float Now = World->GetTimeSeconds();
    if (bDashActive || Now - LastDashTime < DashCooldownSec) return;

    FGameResources& Res = CurrentStats.Resources;
    if (!bDebugDashFreeStamina && (bStaminaExhausted || Res.Stamina < DashStaminaCost)) return;

    // 방향 — 왼손 스틱을 밀고 있으면 그 방향, 중립이면 HMD 정면.
    // 이동과 같은 기준(HMD Yaw)으로 풀어야 스틱을 민 쪽과 튀어나가는 쪽이 일치한다.
    const FRotator CameraYaw(0.f, VRCamera->GetComponentRotation().Yaw, 0.f);
    const FVector Forward = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
    const FVector Right   = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);

    FVector Dir = (Forward * LastMoveInput.Y + Right * LastMoveInput.X).GetSafeNormal2D();
    if (Dir.IsNearlyZero()) Dir = Forward;

    if (!bDebugDashFreeStamina)
    {
        Res.Stamina = FMath::Max(0.f, Res.Stamina - DashStaminaCost);
        if (Res.Stamina <= 0.f)
        {
            bStaminaExhausted = true;
            SetSprinting(false);
        }
        TimeSinceSprintStopped = 0.f;   // 대쉬 직후 곧바로 회복이 시작되지 않도록 지연을 재시작
    }

    bDashActive = true;
    DashTimeRemaining = DashDuration;
    LastDashTime = Now;

    // 액터 회전은 건드리지 않는다 — VR 에서 시야를 강제로 돌리면 즉시 멀미로 이어진다.
    const float DashSpeed = DashDistance / FMath::Max(KINDA_SMALL_NUMBER, DashDuration);
    MovementUtils::BeginFrictionlessLaunch(*this, Dir * DashSpeed, SavedDashFriction);
}

void AVRPawn::UpdateDash(float DeltaTime)
{
    if (!bDashActive) return;

    DashTimeRemaining -= DeltaTime;
    if (DashTimeRemaining <= 0.f)
    {
        StopDash();
    }
}

void AVRPawn::StopDash()
{
    if (!bDashActive) return;
    bDashActive = false;
    DashTimeRemaining = 0.f;

    if (UCharacterMovementComponent* MC = GetCharacterMovement())
    {
        MovementUtils::EndFrictionlessLaunch(*MC, SavedDashFriction);
    }
}

void AVRPawn::OnTurn(const FInputActionValue& Value)
{
    if (IsUIBlockingInput())
    {
        TurnAxisInput = 0.f;
        return;
    }

    const FVector2D Stick = Value.Get<FVector2D>();

    // 인벤토리가 열려 있는 동안 같은 스틱이 회전과 슬롯 이동을 겸하면 아이템을 고르다 몸이 돈다.
    // 회전을 0 으로 확실히 죽이고 선택만 처리한다.
    if (IsInventoryOpen())
    {
        TurnAxisInput = 0.f;
        PlayerUI->NavigateInventory(Stick);
        return;
    }

    // 입력값만 저장 — 실제 회전은 Tick(UpdateSmoothTurn)에서 프레임 보정 적용.
    TurnAxisInput = Stick.X;
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

void AVRPawn::UpdateBodyRotation(float DeltaTime)
{
    if (!VRCamera || !GetMesh()) return;

    // 착석 중에는 의자 방향을 유지하고 고개만 회전
    if (SeatedFurniture.IsValid()) return;

    // HMD(헤드셋)가 바라보는 수평 월드 각도
    const float CameraYaw = VRCamera->GetComponentRotation().Yaw;
    const float DesiredYaw = CameraYaw + BodyMeshYawOffset;

    if (BodyRotationInterpSpeed > 0.f && DeltaTime > KINDA_SMALL_NUMBER)
    {
        const FRotator CurrentRot = GetMesh()->GetComponentRotation();
        const FRotator TargetRot(0.f, DesiredYaw, 0.f);
        const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, BodyRotationInterpSpeed);
        GetMesh()->SetWorldRotation(FRotator(0.f, NewRot.Yaw, 0.f));
    }
    else
    {
        GetMesh()->SetWorldRotation(FRotator(0.f, DesiredYaw, 0.f));
    }
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
    // 인벤토리 열림 중 트리거는 UI 클릭 — 투사체를 쏘지 않는다.
    // 안 막으면 슬롯을 누를 때마다 손앞으로 발사체가 나간다.
    if (PlayerUI && PlayerUI->PressPointer()) return;

    RemoveStateTag(TAG_State_Idle);
    AddStateTag(TAG_State_Action_Combat_Attack);

    // 공격 소음 발생 (NPC 청각 감지용)
    UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), 1.f, this, 0.f, NPCActionKeys::NoiseTag_Attack);

    // 동역학 원거리 — 히트스캔 폐기, 투사체 발사. 오른손 Aim 포즈(조준 정렬) 기준 전방.
    // 명중·데미지는 투사체의 ½mv²(KineticProjectile::OnHit)가 처리. 근접은 스윙 overlap.
    if (ProjectileClass && MotionControllerRightAim && MeleeCombat)
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
            Proj->InitProjectile(this, MeleeCombat->KineticDamageScale, MeleeCombat->MaxKineticDamage);
        }
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

void AVRPawn::OnAttackReleased(const FInputActionValue& /*Value*/)
{
    if (PlayerUI) PlayerUI->ReleasePointer();
}

void AVRPawn::OnAttackMontageEnded(UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
{
    RemoveStateTag(TAG_State_Action_Combat_Attack);
    AddStateTag(TAG_State_Idle);
}

// ============================================================================
// NPC 상호작용
// ============================================================================

void AVRPawn::OnInteract(const FInputActionValue& /*Value*/)
{
    if (IsUIBlockingInput()) return;

    // 착석 중이면 기상이 최우선(토글) — 다른 상호작용 차단.
    if (SeatedFurniture.IsValid())
    {
        StandUpFromFurniture();
        return;
    }

    // 근접 드랍 아이템 획득 — 착석·NPC 감지보다 우선(발밑 아이템을 두고 앉는 오작동 방지).
    if (TryPickupNearby())
    {
        return;
    }

    // 근접 빈 가구가 있으면 착석 우선, 없으면 기존 NPC 대화 타겟팅.
    if (TrySitOnNearbyFurniture())
    {
        return;
    }

    DetectNearbyNPC();
}

bool AVRPawn::TryPickupNearby()
{
    if (!Inventory) return false;

    // HUD 갱신은 Inventory->OnInventoryChanged → PlayerHUDWidget 델리게이트가 자동 처리.
    ADroppedItemBase* Nearest = FindNearestItem(GetActorLocation(), PickupInteractRange);
    return Nearest && Nearest->TryPickupInto(Inventory);
}

ADroppedItemBase* AVRPawn::FindNearestItem(const FVector& Origin, float Radius, bool bIncludeDisplayed) const
{
    UItemManager* ItemManager = UItemManager::Get(this);
    if (!ItemManager) return nullptr;

    ADroppedItemBase* Nearest = nullptr;
    float NearestDist = TNumericLimits<float>::Max();
    for (ADroppedItemBase* Dropped : ItemManager->GetItemsInRange(Origin, Radius))
    {
        // 거래 접시에 올라간 물건은 손으로 못 뺀다 — 올려둔 채 취소를 누르면 인벤토리 반환과
        // 손에 쥔 것이 겹쳐 복사가 된다.
        if (Dropped->bTradeLocked) continue;
        // 진열품은 Interact 픽업 후보에서 뺀다 — 공짜 획득 경로. 그랩은 구매 판정을 타므로 포함.
        if (Dropped->bIsDisplayed && !bIncludeDisplayed) continue;

        // 메시 표면까지 거리 — 긴 물건 끝을 잡아도 중심이 멀다고 옆의 작은 물건에 지지 않게. 충돌이 꺼져 못 재면 액터 위치.
        FVector Closest;
        float Dist = IsValid(Dropped->ItemMesh) ? Dropped->ItemMesh->GetClosestPointOnCollision(Origin, Closest) : -1.f;
        if (Dist < 0.f) Dist = FVector::Dist(Origin, Dropped->GetActorLocation());
        if (Dist < NearestDist)
        {
            NearestDist = Dist;
            Nearest = Dropped;
        }
    }
    return Nearest;
}

void AVRPawn::OnInventoryToggle(const FInputActionValue& /*Value*/)
{
    if (PlayerUI) PlayerUI->ToggleInventory();
}

void AVRPawn::OnMenuToggle(const FInputActionValue& /*Value*/)
{
    if (PlayerUI) PlayerUI->ToggleMenu();
}

void AVRPawn::OnBodyMeasureCapture(const FInputActionValue& /*Value*/)
{
    // IsUIBlockingInput 으로 막지 않는다 — 측정은 메뉴가 열려 있는 동안에만 돌고, 측정 중이 아니면 Capture 가 무시한다.
    if (BodyMeasure) BodyMeasure->Capture();
}

bool AVRPawn::IsUIBlockingInput() const
{
    return PlayerUI && PlayerUI->BlocksGameplayInput();
}

bool AVRPawn::IsInventoryOpen() const
{
    return PlayerUI && PlayerUI->IsInventoryOpen();
}

bool AVRPawn::TrySitOnNearbyFurniture()
{
    UFurnitureManager* Mgr = UFurnitureManager::Get(this);
    if (!Mgr) return false;

    // 반경 내 최근접 빈 착석 가구(Seat/Bed) — 탐색은 매니저 공용 헬퍼.
    // Yaw 만 — 카메라 높이는 불변(멀미 안전).
    AFurnitureActor* Nearest = Mgr->FindNearestVacantSitable(GetActorLocation(), FurnitureInteractRange);
    if (!Nearest || !Nearest->TryOccupyAndSeat(this, /*bYawOnly=*/true)) return false;
    SeatedFurniture = Nearest;

    UE_LOG(LogTemp, Log, TEXT("[VRPawn] 착석: %s"), *Nearest->FurnitureID);
    return true;
}

void AVRPawn::StandUpFromFurniture()
{
    if (AFurnitureActor* Furniture = SeatedFurniture.Get())
    {
        const FTransform SeatXf = Furniture->GetSeatTransform();
        Furniture->Release(this);

        // 좌석 전방 반보 이탈 — 의자 콜리전에 캡슐이 끼는 것 방지.
        const FVector Exit = SeatXf.GetLocation() + SeatXf.GetRotation().GetForwardVector() * 60.f;
        SetActorLocation(Exit, false, nullptr, ETeleportType::TeleportPhysics);

        UE_LOG(LogTemp, Log, TEXT("[VRPawn] 기상: %s"), *Furniture->FurnitureID);
    }
    SeatedFurniture.Reset();
}

// ============================================================================
// 물리 손 쥐기 — 그립으로 월드 아이템을 직접 쥐고, 놓으면 던지거나 NPC 에게 건넨다.
// ============================================================================

void AVRPawn::OnGrabStartRight(const FInputActionValue& Value)       { HandleGripValue(false, Value.Get<float>()); bGripHeld[1] = true; UpdateGrabInput(/*bLeft=*/false); }
void AVRPawn::OnGrabStartLeft(const FInputActionValue& Value)        { HandleGripValue(true, Value.Get<float>());  bGripHeld[0] = true; UpdateGrabInput(/*bLeft=*/true); }
void AVRPawn::OnGrabReleaseRight(const FInputActionValue& /*Value*/) { HandleGripValue(false, 0.f); bGripHeld[1] = false; UpdateGrabInput(/*bLeft=*/false); }
void AVRPawn::OnGrabReleaseLeft(const FInputActionValue& /*Value*/)  { HandleGripValue(true, 0.f);  bGripHeld[0] = false; UpdateGrabInput(/*bLeft=*/true); }
void AVRPawn::OnGrabValueRight(const FInputActionValue& Value)       { HandleGripValue(false, Value.Get<float>()); }
void AVRPawn::OnGrabValueLeft(const FInputActionValue& Value)        { HandleGripValue(true, Value.Get<float>()); }

void AVRPawn::HandleGripValue(bool bLeft, float Value)
{
    UVRHandComponent* Hand = GetHand(bLeft);
    if (!Hand) return;
    Hand->SetGripValue(Value);

    // 그립을 누른 채 손에 든 게 없으면(약하게 쥐어 못 들었음) 매 프레임 다시 쥐어 본다 — 더 세게 쥐면 들린다.
    // 인벤토리가 열렸으면 그립은 슬롯 발동이라 되풀이하지 않는다.
    const int32 h = bLeft ? 0 : 1;
    // 두 번째 손(양손 쥐기)은 인벤토리 슬롯이 비어 있으니 이미 제약으로 쥐고 있는지도 본다 — 안 보면 매 프레임 다시 쥔다.
    if (bGripHeld[h] && bGrabInputActive[h] && !IsInventoryOpen() && Inventory && !Inventory->GetHeldItem(Hand->GetHandSlot()) && !Hand->GetGrabbedItem())
    {
        HandleGrabStart(bLeft);
    }
}

void AVRPawn::UpdateGrabInput(bool bLeft, ADroppedItemBase* Target)
{
    const int32 h = bLeft ? 0 : 1;
    // 핸드트래킹 핀치·주먹은 인벤토리가 열렸을 때 슬롯 발동에만 쓴다 — 월드 아이템은 접촉 쥐기(UpdateContactGrab)가 맡는다.
    // 제스처는 손가락이 이미 물건 속에 들어간 뒤에야 성립해, 그때 쥐면 밀려난 손이 튀며 제약이 끊겼다.
    const UVRHandComponent* Hand = GetHand(bLeft);
    const bool bGesture = IsInventoryOpen() && Hand && Hand->IsGesturing();
    const bool bHeld = bGripHeld[h] || (Hand && Hand->IsContactHeld()) || bGesture;
    if (bHeld == bGrabInputActive[h]) return;
    bGrabInputActive[h] = bHeld;
    if (bHeld) HandleGrabStart(bLeft, Target);
    else       HandleGrabRelease(bLeft);
}

void AVRPawn::HandleGrabStart(bool bLeft, ADroppedItemBase* Target)
{
    const EEquipmentSlot HandSlot = bLeft ? EEquipmentSlot::OffHand : EEquipmentSlot::MainHand;
    UMotionControllerComponent* HandController = bLeft ? MotionControllerLeft : MotionControllerRight;

    // 메뉴가 열린 동안 새로 쥐지 않는다. 이미 쥔 물건은 유지하고 놓기·던지기는 그대로 허용.
    if (IsUIBlockingInput()) return;
    if (!Inventory || Inventory->GetHeldItem(HandSlot) || !HandController) return;

    // 인벤토리를 연 상태의 그립은 "고른 슬롯을 발동한다"는 뜻 — 월드 아이템 줍기와 겹치지 않는다.
    if (IsInventoryOpen())
    {
        PlayerUI->ActivateSelectedSlot(HandSlot);
        return;
    }

    ADroppedItemBase* Nearest = Target ? Target : FindNearestItemNearHand(GrabRadius, bLeft);
    if (!Nearest) return;

    // 물리 손이 없거나 쥐는 힘의 마찰 한계가 무게보다 작으면 안 든다(구매 판정보다 먼저 — 못 들 물건 값을 받지 않게).
    UVRHandComponent* Hand = GetHand(bLeft);
    if (!Hand || !Hand->IsPhysicsActive() || Hand->GetGrabbedItem() || !Hand->CanHold(Nearest)) return;

    // 진열품은 구매가 먼저 — 가판대가 CanAddItem → 골드 차감 → 진열 해제까지 한 번에 판정한다.
    // 거부되면 손에 붙이지 않는다(붙였다 뺏으면 복사 버그 — TradeSession 의 교훈).
    if (Nearest->bIsDisplayed)
    {
        AMerchantStall* Stall = Nearest->DisplayStall.Get();
        if (!Stall || !Stall->TryPurchase(Nearest, Inventory)) return;
    }

    // 물리 손과 아이템을 마찰 한도 제약으로 잇는다(무게·토크를 엔진이 계산).
    if (Hand->GrabWithPhysics(Nearest, /*bKeepCollision=*/Target != nullptr))
    {
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] 쥠: %s"), *Nearest->ItemData.ItemTemplateID);
    }
}

ADroppedItemBase* AVRPawn::FindNearestItemNearHand(float Radius, bool bLeft) const
{
    const UVRHandComponent* Anchor = GetHand(bLeft);
    if (!IsValid(Anchor)) return nullptr;

    // 판정 원점은 폰이 아니라 실제 손(트래킹 앵커) — 손을 뻗은 곳에 있는 것만 걸려야 한다.
    // 앵커는 핸드트래킹 손바닥 관절이다(컨트롤러를 쥐어도). 컨트롤러 위치를 쓰면 내려놓은 컨트롤러 주변을 찾는다.
    return FindNearestItem(Anchor->GetComponentLocation(), Radius, /*bIncludeDisplayed=*/true);
}

void AVRPawn::HandleGrabRelease(bool bLeft)
{
    const EEquipmentSlot HandSlot = bLeft ? EEquipmentSlot::OffHand : EEquipmentSlot::MainHand;
    if (UVRHandComponent* Hand = GetHand(bLeft))
    {
        // 양손으로 쥔 물건 — 두 번째 손이 놓으면 제약만 푼다. 기록 손이 놓을 때 다른 손이 같이 쥐고 있으면 기록만 넘긴다.
        // 마지막 손이 놓을 때만 아래의 수납·판매·건네기·던지기가 일어난다.
        if (Hand->IsSecondaryGrab())
        {
            Hand->ReleaseGrab();
            return;
        }
        if (Hand->HandOverToOtherHand()) return;
        Hand->ReleaseGrab();
    }
    if (!Inventory || !Inventory->GetHeldItem(HandSlot)) return;

    // 그립은 홀드다 — 누르고 있는 동안만 손에 있고, 떼는 순간 어디로 갈지가 여기서 갈린다.
    // 인벤토리를 열어 둔 채 뗐으면 "집어넣겠다"는 뜻이라 회수한다(수납이 detach·해제까지 처리).
    if (IsInventoryOpen())
    {
        Inventory->StoreHeldItem(HandSlot);
        return;
    }

    ADroppedItemBase* Item = Inventory->ReleaseHeldItem(HandSlot);
    if (!Item) return;

    // 상인 매입 상자 안에서 놓았으면 판매 판정. 거부품(퀘스트·BaseValue 0)은 물리가 이미 살아 있어 상자 안에 그대로 떨어진다.
    if (AMerchantStall* Stall = AMerchantStall::FindStallContaining(this, Item->GetActorLocation()))
    {
        Stall->TrySell(Item, Inventory);
        return;
    }

    // 거래 테이블 접시가 먼저다 — 거래 중에 접시 위에서 놓았는데 NPC 인벤토리로 바로
    // 빨려 들어가면 수락/취소를 누를 대상이 사라진다.
    // 세션은 거래 중에만, 그것도 보통 하나만 존재한다. 상시 추적 대신 놓는 순간에만 훑는다.
    TArray<AActor*> Sessions;
    UGameplayStatics::GetAllActorsOfClass(this, ATradeSessionActor::StaticClass(), Sessions);
    for (AActor* Actor : Sessions)
    {
        if (ATradeSessionActor* Session = Cast<ATradeSessionActor>(Actor))
        {
            if (Session->TrySnapItem(Item)) return;
        }
    }

    // 그다음이 건네기 — NPC 앞에서 놓았는데 아이템이 얼굴로 날아가면 곤란하다.
    const FString NpcId = PlayerInteractionUtils::FindNearestNPCId(this, HandOverRange);
    UNPCManager* Manager = NpcId.IsEmpty() ? nullptr : UNPCManager::Get(this);
    ASmartNPC* NPC = Manager ? Manager->GetNPCById(NpcId) : nullptr;
    UInventoryComponent* NpcInv = NPC ? NPC->FindComponentByClass<UInventoryComponent>() : nullptr;

    UItemManager* ItemManager = UItemManager::Get(this);

    FItemData HandOverData;
    if (NpcInv && ItemManager && ItemManager->GetItemDataByID(Item->ItemData.ItemTemplateID, HandOverData))
    {
        // 무게·슬롯이 모자라면 건네지 않고 던지기로 흘려보낸다 — 여기서 액터를 없애면
        // 아이템이 아무 데도 들어가지 않고 증발한다.
        if (NpcInv->AddItem(HandOverData, Item->Amount))
        {
            UE_LOG(LogTemp, Log, TEXT("[VRPawn] 건넴: %s x%d → %s"), *HandOverData.ItemID, Item->Amount, *NpcId);
            Item->ConsumeItem();
            return;
        }
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] 건네기 실패(NPC 공간·무게 부족): %s"), *HandOverData.ItemID);
    }

    // 손 속도를 그대로 실어 던진다. 정지 상태로 놓으면 속도 0 = 그 자리에 떨어진다.
    if (!MeleeCombat) return;
    Item->LaunchThrown(MeleeCombat->GetHandVelocity(bLeft) * ThrowVelocityScale, this,
                       MeleeCombat->KineticDamageScale, MeleeCombat->MaxKineticDamage, MeleeCombat->MeleeStrikeSpeed);
}

bool AVRPawn::StoreHeldItemInInventory()
{
    if (!Inventory) return false;
    return Inventory->StoreHeldItem(EEquipmentSlot::MainHand)
        || Inventory->StoreHeldItem(EEquipmentSlot::OffHand);
}

void AVRPawn::DetectNearbyNPC()
{
    // 최근접이 주민(서버 미등록)이면 로컬 인사(바라보기+Wave+대사 1줄)로 끝. 타겟 NPC 는 건드리지 않는다.
    FString FoundID;
    if (AVillagerCharacter* V = PlayerInteractionUtils::FindNearestTalkTarget(this, 500.f, FoundID))
    {
        V->Interact(this);
        return;
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

FVector AVRPawn::GetHandLocation(bool bRightHand) const
{
    // 보이는 손(물리 손바닥)과 판정 손을 일치시킨다 — 벽 너머로 넣은 실제 손으로는 거래·막기 판정이 안 된다.
    const UVRHandComponent* Hand = GetHand(!bRightHand);
    return Hand ? Hand->GetVisibleLocation() : GetActorLocation();
}

void AVRPawn::OnChatKey()
{
    if (PlayerUI) PlayerUI->OpenChat();
}

void AVRPawn::SayToNpc(const FString& Text)
{
    // 타겟 미지정이면 근접 탐지. player_id 는 고정 "Player" — 서버 affinity·스토리 DB 키(PLAYER_KEY)와 일치.
    // 액터 이름(BP_VRPawn_C_0)으로도 보내면 LLM 2회 호출 + 기록 2배 오염.
    // 최근접이 주민이면 로컬 규칙 응답 — 서버 미전송(NPCManager 미등록). SmartNPC 가 더 가까우면 종전대로.
    FString Nearest;
    if (AVillagerCharacter* V = PlayerInteractionUtils::FindNearestTalkTarget(this, 500.f, Nearest))
    {
        V->RespondToChat(Text);
        return;
    }
    if (CurrentTargetNPCID.IsEmpty()) DetectNearbyNPC();
    if (CurrentTargetNPCID.IsEmpty() || Text.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRPawn] SayToNpc 폐기 — target/text 비어있음"));
        return;
    }
    PlayerInteractionUtils::SendDialogueToNpc(this, TEXT("Player"), CurrentTargetNPCID, Text);
}

// ============================================================================
// 스탯
// ============================================================================

void AVRPawn::ApplyMovementSpeed()
{
    UCharacterMovementComponent* MC = GetCharacterMovement();
    if (!MC) return;

    const float Base = CurrentStats.Movement.WalkSpeed;

    // 자세별 속도 클램프 — Standing 100% / Bending = BendSpeedRatio / Crouching = CrouchSpeed / Prone = 20%
    switch (CurrentPosture)
    {
    case EVRPosture::Standing:
        // Sprint 는 선 자세에서만 — 웅크림/포복은 아래 분기가 각자 속도를 덮어써 자동 억제.
        MC->MaxWalkSpeed = bIsSprinting ? CurrentStats.Movement.SprintSpeed : Base;
        break;
    case EVRPosture::Bending:
        // 전력질주 중 상체가 숙여져도 질주는 유지(스태미나·대시 규칙과 동일), 걷기만 감속.
        MC->MaxWalkSpeed = bIsSprinting ? CurrentStats.Movement.SprintSpeed : Base * BendSpeedRatio;
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
    // 이미 사망 상태에서 추가 피격 시 HandleDeath 중복 호출 → RespawnTimerHandle 이 매번 리셋되어
    // 리스폰 무한 지연되는 버그 방지 (조기 반환).
    if (CurrentStats.Resources.Health <= 0.f)
    {
        return 0.f;
    }

    const float Raw = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    // EnemyCharacter 와 같은 공식: 원시 − 방어력 (최소 0).
    float Actual = FMath::Max(0.f, Raw - CurrentStats.Combat.Defense);

    // 아이템을 쥔 손이 공격자 쪽이면 막기(경감)·패링(무효).
    if (MeleeCombat) Actual = MeleeCombat->ApplyBlock(Actual, DamageCauser);

    CurrentStats.Resources.Health = FMath::Max(0.f, CurrentStats.Resources.Health - Actual);

    if (CurrentStats.Resources.Health <= 0.f)
    {
        HandleDeath();  // Dead 태그 세팅은 PawnDeathUtils::HandleDeath 내부에서 일괄
    }
    return Actual;
}

// ============================================================================
// 체크포인트
// ============================================================================

void AVRPawn::SaveCheckpoint(const FVector& Location, const FRotator& Rotation)
{
    PawnDeathUtils::SaveCheckpoint(CurrentStats, Location, Rotation, Checkpoint, TEXT("VRPawn"));
}

// ============================================================================
// 사망 / 리스폰
// ============================================================================

void AVRPawn::HandleDeath()
{
    // 앉은 채 죽으면 가구가 계속 점유 상태로 남아 아무도 못 쓴다.
    StandUpFromFurniture();

    if (PlayerUI) PlayerUI->CloseInventory();
    SetSprinting(false);
    StopMoveState();

    PawnDeathUtils::HandleDeath(this, GameplayTags,
        RespawnDelay, RespawnTimerHandle,
        FTimerDelegate::CreateUObject(this, &AVRPawn::Respawn), TEXT("VRPawn"));
}

void AVRPawn::Respawn()
{
    PawnDeathUtils::Respawn(this, CurrentStats, Checkpoint, GameplayTags, TEXT("VRPawn"));

    // 텔레포트 전 손 위치가 남아 있으면 다음 프레임 위치 델타가 통째로 스윙 속도로 잡힌다.
    if (MeleeCombat) MeleeCombat->ResetHandVelocity();
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
