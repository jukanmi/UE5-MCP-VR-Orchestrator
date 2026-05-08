#include "VRPawn.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "NativeGameplayTags.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Engine/OverlapResult.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "../NPC/SmartNPC.h"
#include "../NPC/Struct/NPCActionKeys.h"

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

    // 왼손 모션컨트롤러
    MotionControllerLeft = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerLeft"));
    MotionControllerLeft->SetupAttachment(VROrigin);
    MotionControllerLeft->MotionSource = FXRMotionControllerBase::LeftHandSourceId;

    // 오른손 모션컨트롤러
    MotionControllerRight = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerRight"));
    MotionControllerRight->SetupAttachment(VROrigin);
    MotionControllerRight->MotionSource = FXRMotionControllerBase::RightHandSourceId;

    // 손 메시
    LeftHandMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LeftHandMesh"));
    LeftHandMesh->SetupAttachment(MotionControllerLeft);

    RightHandMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RightHandMesh"));
    RightHandMesh->SetupAttachment(MotionControllerRight);

    // 손목 위젯 — 왼손 모션컨트롤러에 부착
    WristWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WristWidget"));
    WristWidgetComp->SetupAttachment(MotionControllerLeft);
    WristWidgetComp->SetRelativeLocation(FVector(0.f, -5.f, 10.f));
    WristWidgetComp->SetRelativeRotation(FRotator(-90.f, 180.f, 0.f));
    WristWidgetComp->SetDrawSize(FVector2D(400.f, 300.f));
    WristWidgetComp->SetWorldScale3D(FVector(0.05f));
    WristWidgetComp->SetVisibility(false);

    // 위젯 인터랙터 — 오른손에서 UI 레이 발사
    WidgetInteractor = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("WidgetInteractor"));
    WidgetInteractor->SetupAttachment(MotionControllerRight);
    WidgetInteractor->InteractionDistance = 300.f;

    // AI 퍼셉션 소스 등록
    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    StimuliSource->RegisterForSense(TSubclassOf<UAISense_Sight>());
    StimuliSource->RegisterWithPerceptionSystem();

    // VR에서는 컨트롤러 회전이 캐릭터 회전에 직접 반영되지 않도록 설정
    bUseControllerRotationYaw  = false;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll  = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;
}

// ============================================================================
// BeginPlay
// ============================================================================

void AVRPawn::BeginPlay()
{
    Super::BeginPlay();

    // HMD 트래킹 원점을 바닥(Floor)으로 설정 — Quest 룸스케일 기준
    UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Floor);

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

    // 손목 위젯에 ChatWidget 클래스 등록
    if (ChatWidgetClass && WristWidgetComp)
    {
        WristWidgetComp->SetWidgetClass(ChatWidgetClass);
        ChatWidgetInstance = Cast<UChatWidget>(WristWidgetComp->GetUserWidgetObject());
    }

    RefreshStats();
    AddStateTag(TAG_State_Idle);
}

// ============================================================================
// Tick
// ============================================================================

void AVRPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    SyncCapsuleToHMD();
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
        if (IA_SnapTurn)      EIC->BindAction(IA_SnapTurn,      ETriggerEvent::Triggered, this, &AVRPawn::OnSnapTurn);
        if (IA_Attack)        EIC->BindAction(IA_Attack,        ETriggerEvent::Started,   this, &AVRPawn::OnAttack);
        if (IA_Interact)      EIC->BindAction(IA_Interact,      ETriggerEvent::Started,   this, &AVRPawn::OnInteract);
        if (IA_ToggleWristUI) EIC->BindAction(IA_ToggleWristUI, ETriggerEvent::Started,   this, &AVRPawn::OnToggleWristUI);
        if (IA_TriggerRight)  EIC->BindAction(IA_TriggerRight,  ETriggerEvent::Triggered, this, &AVRPawn::OnTriggerRight);
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

void AVRPawn::OnSnapTurn(const FInputActionValue& Value)
{
    if (bSnapTurnCooling) return;

    float AxisX = Value.Get<FVector2D>().X;
    if (FMath::Abs(AxisX) < 0.5f) return;  // 데드존

    float TurnDelta = (AxisX > 0.f) ? SnapTurnAngle : -SnapTurnAngle;
    AddActorWorldRotation(FRotator(0.f, TurnDelta, 0.f));

    // 쿨다운 — 연속 회전 방지
    bSnapTurnCooling = true;
    GetWorldTimerManager().SetTimer(
        SnapTurnCooldownTimer,
        [this]() { bSnapTurnCooling = false; },
        SnapTurnCooldown, false);
}

void AVRPawn::SyncCapsuleToHMD()
{
    if (!VRCamera) return;

    // HMD 위치의 XY만 캡슐에 반영 — Z는 캡슐 반높이 기준 유지
    FVector HMDWorld = VRCamera->GetComponentLocation();
    FVector CapsuleBase(HMDWorld.X, HMDWorld.Y, GetActorLocation().Z);
    SetActorLocation(CapsuleBase, false, nullptr, ETeleportType::TeleportPhysics);
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

    // 오른손 컨트롤러 Forward 방향 라인트레이스
    FVector Start     = MotionControllerRight->GetComponentLocation();
    FVector End       = Start + MotionControllerRight->GetForwardVector() * AttackRange;

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params))
    {
        if (AActor* HitActor = Hit.GetActor())
        {
            UE_LOG(LogTemp, Verbose, TEXT("[VRPawn] Attack Hit: %s"), *HitActor->GetName());
            FPointDamageEvent DmgEvent;
            DmgEvent.HitInfo       = Hit;
            DmgEvent.ShotDirection = MotionControllerRight->GetForwardVector();
            DmgEvent.DamageTypeClass = UDamageType::StaticClass();
            HitActor->TakeDamage(AttackDamage, DmgEvent, GetController(), this);
            DrawDebugLine(GetWorld(), Start, Hit.Location, FColor::Red, false, 1.f, 0, 2.f);
            return;
        }
    }
    DrawDebugLine(GetWorld(), Start, End, FColor::Green, false, 0.5f, 0, 1.f);

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

// ============================================================================
// NPC 상호작용
// ============================================================================

void AVRPawn::OnInteract(const FInputActionValue& /*Value*/)
{
    DetectNearbyNPC();
}

void AVRPawn::DetectNearbyNPC()
{
    // 손목 위젯이 열려있으면 닫기
    if (WristWidgetComp && WristWidgetComp->IsVisible())
    {
        WristWidgetComp->SetVisibility(false);
        CurrentTargetNPCID = TEXT("");
        return;
    }

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

    if (!FoundID.IsEmpty())
    {
        CurrentTargetNPCID = FoundID;
        if (ChatWidgetInstance)
            ChatWidgetInstance->CurrentTargetNPCID = CurrentTargetNPCID;
        WristWidgetComp->SetVisibility(true);
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] NPC 발견: %s"), *FoundID);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[VRPawn] 주변 NPC 없음"));
    }
}

// ============================================================================
// UI
// ============================================================================

void AVRPawn::OnToggleWristUI(const FInputActionValue& /*Value*/)
{
    if (WristWidgetComp)
        WristWidgetComp->SetVisibility(!WristWidgetComp->IsVisible());
}

void AVRPawn::OnTriggerRight(const FInputActionValue& Value)
{
    // 위젯 인터랙터가 UI를 가리키고 있으면 트리거를 클릭으로 전달
    if (WidgetInteractor && WidgetInteractor->IsOverInteractableWidget())
    {
        if (Value.Get<float>() > 0.5f)
            WidgetInteractor->PressPointerKey(EKeys::LeftMouseButton);
        else
            WidgetInteractor->ReleasePointerKey(EKeys::LeftMouseButton);
    }
}

// ============================================================================
// 스탯
// ============================================================================

void AVRPawn::ApplyMovementSpeed()
{
    if (UCharacterMovementComponent* MC = GetCharacterMovement())
    {
        MC->MaxWalkSpeed          = CurrentStats.Movement.WalkSpeed;
        MC->MaxWalkSpeedCrouched  = CurrentStats.Movement.CrouchSpeed;
    }
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
// 사망 / 리스폰
// ============================================================================

void AVRPawn::HandleDeath()
{
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
        DisableInput(PC);

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetVisibility(false);
    if (WristWidgetComp) WristWidgetComp->SetVisibility(false);

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
    if (Tag.IsValid()) GameplayTags.AddTag(Tag);
}

void AVRPawn::RemoveStateTag(FGameplayTag Tag)
{
    if (Tag.IsValid()) GameplayTags.RemoveTag(Tag);
}
