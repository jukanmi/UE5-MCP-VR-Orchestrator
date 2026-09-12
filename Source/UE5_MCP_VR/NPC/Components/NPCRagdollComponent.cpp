#include "NPC/Components/NPCRagdollComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NPC/Action/NPCActionComponent.h"
#include "NPC/Action/SmartNPCAIController.h"
#include "NPC/BP/SmartNPC.h"
#include "NPC/Subsystems/NPCManager.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

// 물리 애니메이션 PD — 방향만 애니 포즈로 복원. 위치/속도/최대힘은 0 으로 둬야
// PD 가 위치까지 당겨 래그돌 낙하·충돌과 다투지 않는다(필드 누락 시 흔한 함정).
static FPhysicalAnimationData MakeOrientationPD(float OrientationStrength, float AngularVelStrength)
{
    FPhysicalAnimationData Data;
    Data.bIsLocalSimulation      = false;
    Data.OrientationStrength     = OrientationStrength;
    Data.AngularVelocityStrength = AngularVelStrength;
    Data.PositionStrength        = 0.f;
    Data.VelocityStrength        = 0.f;
    Data.MaxLinearForce          = 0.f;
    Data.MaxAngularForce         = 0.f;
    return Data;
}

// 전신 물리 시뮬 정지 — 본별·컴포넌트 양쪽 끄기 + 메시를 캡슐 기준 제자리로 복귀
// (기상 블렌드 완료·기상 마무리 공용). 시뮬 중에는 물리 바디가 메시 트랜스폼을 덮어쓰므로,
// 끄기만 하고 두면 누운 자세가 컴포넌트에 남아 캡슐만 서고 몸은 누운 그림이 된다.
static void StopBodySimulation(USkeletalMeshComponent* Mesh, const FTransform& DefaultRelative)
{
    Mesh->SetAllBodiesSimulatePhysics(false);
    Mesh->SetSimulatePhysics(false);
    Mesh->SetRelativeTransform(DefaultRelative);
}

UNPCRagdollComponent::UNPCRagdollComponent()
{
    // Tick 은 Flinch 램프·넉다운 물리 진행 중에만 — RefreshTickEnabled 가 토글.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    // 기상 몽타주 기본값 — BP_SmartNPC 가 액터 프로퍼티에 갖고 있던 값을 C++ 로 확정(2026-09-12).
    // 바이너리에만 두면 새 NPC BP 마다 다시 맞춰야 한다. 에셋이 없으면 nullptr → 즉시 블렌드 복귀 폴백.
    static ConstructorHelpers::FObjectFinder<UAnimMontage> FaceUp(TEXT("/Game/Core/Animation/interact/lay/AM_LayUp.AM_LayUp"));
    static ConstructorHelpers::FObjectFinder<UAnimMontage> FaceDown(TEXT("/Game/Core/Animation/AS_stand_up.AS_stand_up"));
    if (FaceUp.Succeeded())   GetUpMontage_FaceUp = FaceUp.Object;
    if (FaceDown.Succeeded()) GetUpMontage_FaceDown = FaceDown.Object;
}

// --- 소유자 접근 ---

ACharacter* UNPCRagdollComponent::GetOwnerCharacter() const
{
    return Cast<ACharacter>(GetOwner());
}

USkeletalMeshComponent* UNPCRagdollComponent::GetOwnerMesh() const
{
    ACharacter* C = GetOwnerCharacter();
    return C ? C->GetMesh() : nullptr;
}

bool UNPCRagdollComponent::IsOwnerDead() const
{
    const ASmartNPC* NPC = Cast<ASmartNPC>(GetOwner());
    return NPC && NPC->bIsDead;
}

FString UNPCRagdollComponent::GetOwnerAgentID() const
{
    const ASmartNPC* NPC = Cast<ASmartNPC>(GetOwner());
    return NPC ? NPC->AgentID : GetNameSafe(GetOwner());
}

// --- 수명 ---

void UNPCRagdollComponent::BeginPlay()
{
    Super::BeginPlay();

    USkeletalMeshComponent* MeshComp = GetOwnerMesh();
    if (!MeshComp) return;

    // Flinch 상체 PD 구동기 — 소유 메시에 바인딩. 액터 서브오브젝트가 아니라 여기서 만드는 이유:
    // 래그돌이 필요로 하는 것을 래그돌 컴포넌트가 전부 갖게 하려고.
    PhysicalAnim = NewObject<UPhysicalAnimationComponent>(GetOwner(), TEXT("PhysicalAnim"));
    PhysicalAnim->RegisterComponent();
    PhysicalAnim->SetSkeletalMeshComponent(MeshComp);

    // 기상 후 복원용 원본 캡처. 래그돌 전 자세는 물리가 덮어쓰기 전에 잡아 둬야 되돌릴 기준이 생긴다.
    OriginalMeshProfile = MeshComp->GetCollisionProfileName();
    OriginalMeshCollision = MeshComp->GetCollisionEnabled();
    DefaultMeshRelativeTransform = MeshComp->GetRelativeTransform();
}

void UNPCRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 넉다운 도중 파괴/종료 시 동시 카운트 누수 방지(UNPCManager 소유 카운터 감소).
    LeaveKnockdownPhase();
    Super::EndPlay(EndPlayReason);
}

void UNPCRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bFlinching)
    {
        TickFlinchRamp(DeltaTime);
    }
    if (KnockdownPhase == EKnockdownPhase::Ragdoll)
    {
        TickSettleDetection(DeltaTime);
    }
    else if (KnockdownPhase == EKnockdownPhase::GettingUp)
    {
        TickGetUpBlend(DeltaTime);
    }
}

void UNPCRagdollComponent::RefreshTickEnabled()
{
    SetComponentTickEnabled(bFlinching || KnockdownPhase != EKnockdownPhase::None);
}

void UNPCRagdollComponent::LeaveKnockdownPhase()
{
    if (KnockdownPhase == EKnockdownPhase::None) return;
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(GetUpMontageTimer);
    }
    if (UNPCManager* Mgr = UNPCManager::Get(this)) { Mgr->ExitKnockdown(); }
    KnockdownPhase = EKnockdownPhase::None;
}

// --- 진입점 ---

void UNPCRagdollComponent::NoteHit(FName Bone, const FVector& Direction)
{
    LastHitBone = Bone;
    LastHitDirection = Direction;
}

// 피격 강도(=½mv² 데미지)로 반응 분기. 방향·본은 직전 NoteHit 가 채운 LastHit* 사용.
void UNPCRagdollComponent::ReactToHit(float HitStrength)
{
    if (IsOwnerDead()) return;

    if (HitStrength >= KnockdownImpulseThreshold)
    {
        Knockdown();
    }
    else
    {
        // Flinch 내부에서 넉다운/기상 중이면 무시(약타는 강반응 덮어쓰지 않음).
        Flinch();
    }
}

void UNPCRagdollComponent::EnterDeathRagdoll()
{
    // 진행 중이던 넉다운/기상은 정리(타이머·동시카운트). 이미 시뮬 중이면 EnterRagdoll 이 임펄스만 갱신(중복 진입 아님).
    LeaveKnockdownPhase();
    bFlinching = false;
    EnterRagdoll(/*bFatal=*/true);
    RefreshTickEnabled();
}

// 사망·넉다운 공유 — 전신 래그돌 진입. 전제: 메시 Physics Asset 필수.
void UNPCRagdollComponent::EnterRagdoll(bool bFatal)
{
    ACharacter* OwnerChar = GetOwnerCharacter();
    USkeletalMeshComponent* MeshComp = GetOwnerMesh();
    if (!OwnerChar || !MeshComp) return;

    // 진행 중 몽타주 정지(애니가 물리와 위치 다툼 방지).
    OwnerChar->StopAnimMontage();

    // 캡슐 충돌 끄기(래그돌이 자기 캡슐에 걸려 뜨는 것 방지).
    if (UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    // 이동 컴포넌트 정지(물리와 위치 다툼 방지).
    if (UCharacterMovementComponent* CMC = OwnerChar->GetCharacterMovement())
    {
        CMC->StopMovementImmediately();
        CMC->DisableMovement();
    }

    // 순수 래그돌 — 직전 Flinch 가 남긴 상체 PD 제거. PD 가 남아 있으면 서기 애니가 몸을 당겨 낙하 충돌이 깨진다.
    // 기본 FPhysicalAnimationData 는 전 강도 0 → 사실상 PD off.
    if (PhysicalAnim)
    {
        PhysicalAnim->ApplyPhysicalAnimationSettingsBelow(KnockdownPelvisBone, FPhysicalAnimationData(), /*bIncludeSelf=*/true);
    }
    FlinchBlendWeight = 0.f;

    // 전신 물리 시뮬 ON + 블렌드 1(완전 물리). 기상 시 블렌드를 0 으로 램프.
    MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComp->SetAllBodiesSimulatePhysics(true);
    MeshComp->SetSimulatePhysics(true);
    MeshComp->SetAllBodiesPhysicsBlendWeight(1.0f);
    MeshComp->WakeAllRigidBodies();

    // 마지막 타격 방향 임펄스 — 사망=DeathImpulseStrength, 넉다운=×KnockdownImpulseScale.
    const float ImpulseMag = bFatal ? DeathImpulseStrength : (DeathImpulseStrength * KnockdownImpulseScale);
    if (ImpulseMag > 0.f && !LastHitDirection.IsNearlyZero())
    {
        // 피격 본에 물리 바디 없으면(손가락·무기·버추얼 본) 임펄스 무시됨 → 골반 폴백.
        FName ImpulseBone = LastHitBone;
        if (ImpulseBone == NAME_None || !MeshComp->GetBodyInstance(ImpulseBone))
        {
            ImpulseBone = KnockdownPelvisBone;
        }
        MeshComp->AddImpulse(LastHitDirection * ImpulseMag, ImpulseBone, /*bVelChange=*/false);
    }
}

// 약타 — 상체(FlinchRootBone 이하) 물리 블렌드 + 임펄스. Tick 램프(TickFlinchRamp)가 애니로 복귀.
void UNPCRagdollComponent::Flinch()
{
    ACharacter* OwnerChar = GetOwnerCharacter();
    USkeletalMeshComponent* MeshComp = GetOwnerMesh();
    if (!OwnerChar || !MeshComp || !PhysicalAnim || IsOwnerDead()) return;

    // 넉다운/기상 중에는 약타 반응 생략 — 전신 래그돌이 우선.
    if (KnockdownPhase != EKnockdownPhase::None) return;

    // 1) PD — 시뮬 본을 매 프레임 애니 포즈로 끌어당김(위치는 자유, 방향만 복원).
    const FPhysicalAnimationData Data = MakeOrientationPD(FlinchOrientationStrength, FlinchAngularVelStrength);
    PhysicalAnim->ApplyPhysicalAnimationSettingsBelow(FlinchRootBone, Data, /*bIncludeSelf=*/true);

    // 2) 메시 물리 콜리전 허용(QueryOnly 면 AddImpulse 거부) + 상체 시뮬 ON + 블렌드 1.
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComp->SetAllBodiesBelowSimulatePhysics(FlinchRootBone, true, /*bIncludeSelf=*/true);
    MeshComp->SetAllBodiesBelowPhysicsBlendWeight(FlinchRootBone, 1.0f, /*bSkipCustomPhysicsType=*/false, /*bIncludeSelf=*/true);
    FlinchBlendWeight = 1.0f;

    // 3) 피격 임펄스 — 실제 타격 방향. Flinch 는 상체만 시뮬하므로 하체 피격 본은 효과 없음 → 시뮬 중인 본만,
    //    아니면 가슴(Spine2) 폴백.
    const FVector Dir = LastHitDirection.IsNearlyZero() ? -OwnerChar->GetActorForwardVector() : LastHitDirection;
    FName Bone = FName(TEXT("Spine2"));
    if (LastHitBone != NAME_None && MeshComp->IsSimulatingPhysics(LastHitBone))
    {
        Bone = LastHitBone;
    }
    MeshComp->AddImpulse(Dir * FlinchImpulse, Bone, /*bVelChange=*/false);

    bFlinching = true;
    RefreshTickEnabled();
}

// Flinch 복귀 램프 — PhysicsBlendWeight 1→0 보간, 0 도달 시 시뮬 off + 콜리전 원복.
void UNPCRagdollComponent::TickFlinchRamp(float DeltaSeconds)
{
    USkeletalMeshComponent* MeshComp = GetOwnerMesh();
    if (!MeshComp) return;

    FlinchBlendWeight = FMath::FInterpConstantTo(FlinchBlendWeight, 0.f, DeltaSeconds, FlinchRecoverSpeed);
    MeshComp->SetAllBodiesBelowPhysicsBlendWeight(FlinchRootBone, FlinchBlendWeight, false, true);

    if (FlinchBlendWeight <= KINDA_SMALL_NUMBER)
    {
        MeshComp->SetAllBodiesBelowSimulatePhysics(FlinchRootBone, false, true);
        MeshComp->SetCollisionEnabled(OriginalMeshCollision);  // 하드코딩 QueryOnly 대신 원본 복원
        bFlinching = false;
        RefreshTickEnabled();
    }
}

// 강타 — 전신 래그돌 + AI 정지 + 안착 후 기상. 넉다운/기상 중 재호출 시 재진입(저글 허용 — 가드 없음이 의도).
void UNPCRagdollComponent::Knockdown()
{
    ACharacter* OwnerChar = GetOwnerCharacter();
    if (!OwnerChar || !GetOwnerMesh() || IsOwnerDead()) return;

    const bool bReentry = (KnockdownPhase != EKnockdownPhase::None);

    // 기상 중 재타격 → 몽타주·타이머 취소하고 다시 쓰러뜨림.
    if (KnockdownPhase == EKnockdownPhase::GettingUp)
    {
        OwnerChar->StopAnimMontage();
        GetWorld()->GetTimerManager().ClearTimer(GetUpMontageTimer);
    }

    if (!bReentry)
    {
        // 동시 넉다운 상한 초과 → Flinch 폴백(전신 래그돌은 비용·시야 혼잡).
        // 카운터는 UNPCManager 소유(PIE 세션별 리셋). 매니저 없으면 게이트 없이 진행.
        if (UNPCManager* Mgr = UNPCManager::Get(this))
        {
            if (!Mgr->TryEnterKnockdown(MaxConcurrentKnockdown))
            {
                UE_LOG(LogTemp, Verbose, TEXT("[Ragdoll] %s 동시 넉다운 상한(%d) → Flinch 폴백"),
                    *GetOwnerAgentID(), MaxConcurrentKnockdown);
                Flinch();
                return;
            }
        }

        // AI 정지 + 진행 액션 중지(최초 진입만 — 재진입 시 이미 정지).
        if (ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(OwnerChar->GetController()))
        {
            AI->PauseAI();
        }
        if (UNPCActionComponent* ActionComp = OwnerChar->FindComponentByClass<UNPCActionComponent>())
        {
            ActionComp->StopAllActions();
        }
    }

    bFlinching = false;
    KnockdownPhase = EKnockdownPhase::Ragdoll;
    SettleTimer = 0.f;

    // 전신 래그돌(새 임펄스 포함). 재진입이면 이미 시뮬 중 → 임펄스만 갱신됨.
    EnterRagdoll(/*bFatal=*/false);

    RefreshTickEnabled();
}

// 안착 감지 — 골반 선속도가 임계 미만으로 SettleHoldTime 지속되면 기상 시작.
void UNPCRagdollComponent::TickSettleDetection(float DeltaSeconds)
{
    USkeletalMeshComponent* MeshComp = GetOwnerMesh();
    if (!MeshComp) return;

    // 골반 본에 피직스 바디가 없으면 GetPhysicsLinearVelocity 가 0 을 반환 → 공중 낙하 중에도
    // 즉시 안착 판정되는 버그. 바디 없으면 루트 바디(NAME_None) 속도로 폴백.
    FName VelBone = KnockdownPelvisBone;
    if (!MeshComp->GetBodyInstance(VelBone))
    {
        VelBone = NAME_None;
    }
    const FVector PelvisVel = MeshComp->GetPhysicsLinearVelocity(VelBone);
    if (PelvisVel.Size() < SettleSpeedThreshold)
    {
        SettleTimer += DeltaSeconds;
        if (SettleTimer >= SettleHoldTime)
        {
            BeginGetUp();
        }
    }
    else
    {
        SettleTimer = 0.f;
    }
}

// 기상 준비 — 엎/누움 판정, 캡슐 바닥 재배치·콜리전 복원, 기상 몽타주 재생 + 블렌드 램프 시작.
void UNPCRagdollComponent::BeginGetUp()
{
    ACharacter* OwnerChar = GetOwnerCharacter();
    USkeletalMeshComponent* MeshComp = GetOwnerMesh();
    if (!OwnerChar || !MeshComp) return;

    KnockdownPhase = EKnockdownPhase::GettingUp;

    // 1) 엎/누움 판정 — Hips 본의 up 축(GetAxisZ)과 월드 up 내적. >=0 이면 등이 바닥(FaceUp).
    //    GetSocketQuaternion = 명시적 월드 공간. GetAxisZ = 쿼터니언의 Z(up)축.
    const FVector HipsUp = MeshComp->GetSocketQuaternion(KnockdownPelvisBone).GetAxisZ();
    const bool bFaceUp = FVector::DotProduct(HipsUp, FVector::UpVector) >= 0.f;

    // 2) 캡슐 재배치 — Hips 수평 위치, 바닥 트레이스 Z + 캡슐 반높이.
    const FVector HipsLoc = MeshComp->GetBoneLocation(KnockdownPelvisBone);
    float GroundZ = HipsLoc.Z;
    UWorld* W = GetWorld();
    if (W)
    {
        FHitResult Hit;
        // 시작은 골반 살짝 위(+20)만 — 골반은 바닥에 누운 상태라 +100 이면 머리 위 테이블·천장을 바닥으로 오인.
        const FVector Start = HipsLoc + FVector(0.f, 0.f, 20.f);
        const FVector End   = HipsLoc - FVector(0.f, 0.f, 500.f);
        FCollisionQueryParams Params(FName(TEXT("GetUpFloor")), /*bTraceComplex=*/false, OwnerChar);
        // 정적 지형만 — Visibility 면 타 NPC/플레이어/트리거 위로 텔레포트 위험.
        if (W->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
        {
            GroundZ = Hit.Location.Z;
        }
    }
    float HalfHeight = 88.f;
    if (UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent())
    {
        HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    }
    FVector NewLoc(HipsLoc.X, HipsLoc.Y, GroundZ + HalfHeight);

    // 캡슐 Yaw 를 래그돌이 누운 방향(Hips Yaw)에 정렬 — 안 하면 기상 몽타주가 쓰러지기 전 회전으로 시작해 스냅.
    // faceUp(등 바닥)은 몸이 뒤집혀 있어 +180 보정(리그·몽타주별 PIE 튜닝 대상).
    FRotator NewRot(0.f, MeshComp->GetSocketRotation(KnockdownPelvisBone).Yaw, 0.f);
    if (bFaceUp)
    {
        NewRot.Yaw += 180.f;
    }

    // 캡슐 콜리전 먼저 복원 — FindTeleportSpot 이 캡슐 충돌형상으로 겹침 검사하므로 NoCollision 이면 무효.
    if (UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }

    // 래그돌이 벽·장애물 구석에 박혀 멈췄을 때 캡슐 강제 텔레포트 시 끼임 방지 — 안전 위치 탐색.
    if (W)
    {
        FVector SafeLoc = NewLoc;
        if (W->FindTeleportSpot(OwnerChar, SafeLoc, NewRot))
        {
            NewLoc = SafeLoc;
        }
    }

    // 물리 텔레포트(sweep 생략)로 안전 위치·방향 배치.
    OwnerChar->SetActorLocationAndRotation(NewLoc, NewRot, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

    if (UCharacterMovementComponent* CMC = OwnerChar->GetCharacterMovement())
    {
        CMC->SetMovementMode(MOVE_Walking);
    }

    // 3) 기상 몽타주 + 전신 블렌드 램프(시뮬→애니). 현재 블렌드 1 에서 Tick 이 0 으로.
    GetUpBlendWeight = 1.0f;
    UAnimMontage* Montage = bFaceUp ? GetUpMontage_FaceUp : GetUpMontage_FaceDown;
    if (Montage)
    {
        const float Dur = OwnerChar->PlayAnimMontage(Montage);
        // 정상 종료는 EndDelegate 로 감지(타이머는 배속·인터럽트에 어긋남). bInterrupted 는 무시.
        if (UAnimInstance* Anim = MeshComp->GetAnimInstance())
        {
            FOnMontageEnded EndDel;
            EndDel.BindUObject(this, &UNPCRagdollComponent::OnGetUpMontageEnded);
            Anim->Montage_SetEndDelegate(EndDel, Montage);
        }
        // 워치독 — 델리게이트 누락·타 몽타주에 의한 인터럽트로 GettingUp 영구 고착 방지.
        GetWorld()->GetTimerManager().SetTimer(GetUpMontageTimer, this, &UNPCRagdollComponent::FinishGetUp,
            FMath::Max(Dur, 0.1f) + 0.5f, false);
    }
    else
    {
        // 폴백 — 몽타주 미할당 시 즉시 블렌드 복귀. 짧은 타이머로 마무리.
        GetWorld()->GetTimerManager().SetTimer(GetUpMontageTimer, this, &UNPCRagdollComponent::FinishGetUp, 0.5f, false);
    }

    UE_LOG(LogTemp, Log, TEXT("[Ragdoll] %s 기상 시작 (faceUp=%d, montage=%s)"),
        *GetOwnerAgentID(), bFaceUp ? 1 : 0, Montage ? *Montage->GetName() : TEXT("none(fallback)"));

    RefreshTickEnabled();
}

// 기상 블렌드 램프 — 전신 PhysicsBlendWeight 1→0, 0 도달 시 시뮬 off.
void UNPCRagdollComponent::TickGetUpBlend(float DeltaSeconds)
{
    USkeletalMeshComponent* MeshComp = GetOwnerMesh();
    if (!MeshComp) return;

    // weight 0 도달 후 몽타주 끝날 때까지 매 프레임 무거운 물리 설정 반복 방지 — 보간 중에만 처리.
    if (GetUpBlendWeight > 0.f)
    {
        GetUpBlendWeight = FMath::FInterpConstantTo(GetUpBlendWeight, 0.f, DeltaSeconds, FlinchRecoverSpeed);
        MeshComp->SetAllBodiesPhysicsBlendWeight(GetUpBlendWeight);

        if (GetUpBlendWeight <= KINDA_SMALL_NUMBER)
        {
            GetUpBlendWeight = 0.f;
            StopBodySimulation(MeshComp, DefaultMeshRelativeTransform);
        }
    }
}

// 기상 몽타주 종료 델리게이트 — 정상 완료만 처리. 인터럽트(재넉다운·타 몽타주)는 무시.
void UNPCRagdollComponent::OnGetUpMontageEnded(UAnimMontage* /*Montage*/, bool bInterrupted)
{
    if (bInterrupted) return;
    if (KnockdownPhase == EKnockdownPhase::GettingUp)
    {
        FinishGetUp();
    }
}

// 기상 완료 — 메시 콜리전 원복, AI 재개, 평상 복귀. (몽타주 종료 델리게이트·워치독·폴백에서 호출)
void UNPCRagdollComponent::FinishGetUp()
{
    if (IsOwnerDead()) return;

    if (USkeletalMeshComponent* MeshComp = GetOwnerMesh())
    {
        MeshComp->SetAllBodiesPhysicsBlendWeight(0.f);
        StopBodySimulation(MeshComp, DefaultMeshRelativeTransform);
        // 원본 프로파일·활성화 상태 복원 — Ragdoll 프로파일/QueryAndPhysics 잔존 방지.
        MeshComp->SetCollisionProfileName(OriginalMeshProfile);
        MeshComp->SetCollisionEnabled(OriginalMeshCollision);
    }

    LeaveKnockdownPhase();
    SettleTimer = 0.f;
    GetUpBlendWeight = 0.f;

    // AI 재개 — StateTree 재시작(루트부터 재평가 = 기상 후 위협 재판단).
    if (ACharacter* OwnerChar = GetOwnerCharacter())
    {
        if (ASmartNPCAIController* AI = Cast<ASmartNPCAIController>(OwnerChar->GetController()))
        {
            AI->ResumeAI();
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Ragdoll] %s 기상 완료 — AI 재개"), *GetOwnerAgentID());
    RefreshTickEnabled();
}
