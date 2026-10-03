#include "Core/BP/VRHandComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/BP/CombatCharacter.h"
#include "Core/BP/VRPawnAnimInstance.h"
#include "Core/Physics/NPCBoneCapsuleSet.h"
#include "Core/Types/CollisionChannels.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "IXRTrackingSystem.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Components/InventoryComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace
{
    // 손 바디는 벽(WorldStatic/WorldDynamic)·물리 물체(PhysicsBody — 드랍 아이템·래그돌)·NPC 캡슐(Pawn)·반대쪽 손에 막히고 나머지는 무시한다.
    // 아이템과의 접촉(밀기·받치기·쳐내기)은 Chaos 가 벽과 같은 규칙으로 푼다 — 들고 있는 아이템은 QueryOnly 라 손과 안 부딪힌다.
    // 같은 손 채널을 무시해야 주먹(손끝↔손바닥)·핀치(손끝↔손끝)가 자기 몸체에 걸리지 않는다.
    // Pawn 을 막으므로 자기 캡슐·몸 메시는 폰 BeginPlay 에서 손 채널을 무시하게 한다.
    // Visibility 를 막으면 UI 포인터 광선을 가린다.
    void SetupHandBodyCollision(UPrimitiveComponent* Body, bool bLeft)
    {
        Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Body->SetCollisionObjectType(bLeft ? ECC_HandLeft : ECC_HandRight);
        Body->SetCollisionResponseToAllChannels(ECR_Ignore);
        Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        Body->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
        Body->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
        Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Body->SetCollisionResponseToChannel(bLeft ? ECC_HandRight : ECC_HandLeft, ECR_Block);
        Body->SetGenerateOverlapEvents(false);
        Body->SetEnableGravity(false);   // 드라이브가 중력을 이기느라 손이 처지지 않게
    }

    // 앵커 프레임 → 손바닥 상자 프레임. 상자 두께축(Z)이 손바닥 법선이어야 한다.
    // 핸드트래킹 손바닥 관절은 Z 가 곧 손등 방향이라 그대로, 컨트롤러 Grip 포즈는 손바닥 법선이 Y 라 X 축으로 90° 돌린다.
    FQuat PalmShapeOffset(bool bHandTracked)
    {
        return bHandTracked ? FQuat::Identity : FRotator(0.f, 0.f, 90.f).Quaternion();
    }

    // Chaos 가 회전 드라이브 강성에 곱하는 배율(엔진 기본 1.5). 콘솔 변수가 없으면 1.
    float AngularDriveStiffnessScale()
    {
        static const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveStiffnessScale"));
        return CVar ? CVar->GetFloat() : 1.f;
    }

    // 접촉 쥐기 짝 값 — 1~4 = 엄지의 짝 손가락(핀치), 이 값 = 손바닥과 손가락들로 감싸 쥠(주먹).
    constexpr int32 PalmGripPartner = 5;
}

UVRHandComponent::UVRHandComponent()
{
    PrimaryComponentTick.bCanEverTick = false;   // 폰이 순서대로 부른다
}

UVRPawnAnimInstance* UVRHandComponent::GetAnim() const
{
    const ACharacter* Owner = Cast<ACharacter>(GetOwner());
    return Owner && Owner->GetMesh() ? Cast<UVRPawnAnimInstance>(Owner->GetMesh()->GetAnimInstance()) : nullptr;
}

UInventoryComponent* UVRHandComponent::GetInventory() const
{
    return GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
}

bool UVRHandComponent::IsPhysicsActive() const
{
    return Palm && Palm->IsSimulatingPhysics();
}

FVector UVRHandComponent::GetVisibleLocation() const
{
    // 보이는 손(물리 손바닥)과 판정 손을 일치시킨다 — 벽 너머로 넣은 실제 손으로는 거래·막기 판정이 안 된다.
    return IsPhysicsActive() ? Palm->GetComponentLocation() : GetComponentLocation();
}

// ============================================================================
// 물리 손 생성
// ============================================================================

void UVRHandComponent::InitPhysics()
{
    AActor* Owner = GetOwner();
    if (!Owner || Palm) return;
    const bool bLeft = IsLeft();
    const TCHAR* Side = bLeft ? TEXT("Left") : TEXT("Right");

    // 물리 손바닥 — 바디(손바닥 관절에 중심). 실제 치수는 ApplyColliderSizes 가 넣는다.
    // 빠른 손놀림에 얇은 벽·지형을 건너뛰지 않게. CCD 가 아니라 MACD — 양손 모두 CCD 면 맞닿을 때 한 프레임은 충돌 순간으로
    // 되감고 다음 프레임은 파고들었다 튕겨 나와 2프레임 주기로 떨었다(PIE 실측 0.56cm/프레임 → MACD 0).
    Palm = NewObject<UBoxComponent>(Owner, *FString::Printf(TEXT("PhysicsPalm%s"), Side));
    Palm->InitBoxExtent(FVector(4.5f, 4.25f, 1.5f));
    SetupHandBodyCollision(Palm, bLeft);
    Palm->SetUseMACD(true);
    Palm->SetWorldLocationAndRotation(GetComponentLocation(), FQuat::Identity);
    Palm->RegisterComponent();
    Palm->SetSimulatePhysics(true);

    // 제약은 손바닥 ↔ 월드. 이동·회전 모두 자유 — 드라이브만이 바디를 목표 위치·회전 쪽으로 당긴다.
    PalmConstraint = NewObject<UPhysicsConstraintComponent>(Owner, *FString::Printf(TEXT("PalmConstraint%s"), Side));
    PalmConstraint->SetUsingAbsoluteLocation(true);
    PalmConstraint->SetUsingAbsoluteRotation(true);
    PalmConstraint->RegisterComponent();
    PalmConstraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Free, 0.f);
    PalmConstraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Free, 0.f);
    PalmConstraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Free, 0.f);
    PalmConstraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0.f);
    PalmConstraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Free, 0.f);
    PalmConstraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.f);

    // 강성은 위치 드라이브, 감쇠는 속도 드라이브에 걸리므로 둘 다 켠다.
    PalmConstraint->SetLinearPositionDrive(true, true, true);
    PalmConstraint->SetLinearVelocityDrive(true, true, true);
    PalmConstraint->SetLinearVelocityTarget(FVector::ZeroVector);
    PalmConstraint->SetLinearDriveParams(DriveStiffness, DriveDamping, DriveMaxForce);
    PalmConstraint->SetLinearPositionTarget(FVector::ZeroVector);

    // 회전은 SLERP 한 축으로 — 스윙/트위스트 분리 드라이브는 큰 회전에서 짐벌처럼 꺾인다.
    PalmConstraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
    PalmConstraint->SetOrientationDriveSLERP(true);
    PalmConstraint->SetAngularVelocityDriveSLERP(true);
    PalmConstraint->SetAngularVelocityTarget(FVector::ZeroVector);
    PalmConstraint->SetAngularDriveParams(AngularStiffness, AngularDamping, 0.f);   // 0 = 토크 제한 없음
    PalmConstraint->SetAngularOrientationTarget(FRotator::ZeroRotator);

    // 제약 프레임을 월드 축 정렬로 바디 중심에 두고 마지막에 연결한다. 월드 쪽 프레임은 이 순간의
    // 제약 월드 변환으로 고정되므로, 위치 목표 = (목표 월드 위치 − 제약 월드 위치), 회전 목표 = 목표 월드 회전.
    PalmConstraint->SetWorldLocationAndRotation(Palm->GetComponentLocation(), FQuat::Identity);
    PalmConstraint->SetConstrainedComponents(Palm, NAME_None, nullptr, NAME_None);

    // 손바닥 모양 — 바디는 작은 핵이고, 메시 손바닥에 맞춘 상자(손목 쪽·손가락 쪽)를 용접해 실제 손바닥 자리에 놓는다.
    for (int32 Part = 0; Part < UVRPawnAnimInstance::NumPalmShapes; ++Part)
    {
        UBoxComponent* Shape = NewObject<UBoxComponent>(Owner, *FString::Printf(TEXT("PalmShape%s%d"), Side, Part));
        Shape->InitBoxExtent(Palm->GetUnscaledBoxExtent());
        SetupHandBodyCollision(Shape, bLeft);
        Shape->SetWorldLocationAndRotation(Palm->GetComponentLocation(), Palm->GetComponentQuat());
        Shape->RegisterComponent();
        Shape->AttachToComponent(Palm, FAttachmentTransformRules(EAttachmentRule::KeepWorld, true));
        PalmShapes.Add(Shape);
    }

    // 손가락 캡슐 — 손가락마다 가운데 마디·끝마디(엄지는 첫 마디·끝마디). 첫 마디는 손바닥 상자와 겹쳐 두지 않는다.
    // 따로 움직이는 바디가 아니라 손바닥 바디에 용접된 모양이라, 어느 마디가 닿든 엔진이 손 전체를 한 덩어리로 멈춘다.
    // 핸드트래킹이 잡히기 전엔 충돌을 끄고 손바닥 안에 둔다.
    for (int32 j = 0; j < UVRPawnAnimInstance::NumFingerShapes; ++j)
    {
        UCapsuleComponent* Body = NewObject<UCapsuleComponent>(Owner, *FString::Printf(TEXT("Finger%s%d_%d"), Side,
                                                                                    j / UVRPawnAnimInstance::ShapesPerFinger, j % UVRPawnAnimInstance::ShapesPerFinger));
        Body->InitCapsuleSize(1.f, 2.f);   // 실제 치수는 애님 인스턴스가 메시에서 뽑은 뒤 ApplyColliderSizes 가 넣는다
        SetupHandBodyCollision(Body, bLeft);
        Body->SetCollisionResponseToAllChannels(ECR_Ignore);
        Body->SetWorldLocation(Palm->GetComponentLocation());
        Body->RegisterComponent();
        Body->AttachToComponent(Palm, FAttachmentTransformRules(EAttachmentRule::KeepWorld, true));
        FingerBodies.Add(Body);
    }

    // 물리 쥐기 제약 — 잡을 때 손 바디와 아이템을 쥔 중심에서 잇는다. 이동·회전은 자유, 드라이브가 쥔 자세로 당기되
    // 드라이브 최대 힘·토크를 마찰 한계로 둔다 — 넘치는 만큼 미끄러지거나 돈다(SyncGrab 이 매 틱 한계·목표 갱신).
    // 힘 모드(가속도 모드 끔) — 가속도 모드면 상한이 역질량으로 나뉘어 μ·N 이 힘이 아니게 된다.
    // 회전 토크 상한은 DefaultEngine.ini 의 p.Chaos.Solver.Joint.UseSimd=0 이어야 먹는다(엔진 SIMD 경로가 클램프 생략).
    // 손 ↔ 쥔 물건 충돌 여부는 쥘 때 방식별로 정한다.
    GrabConstraint = NewObject<UPhysicsConstraintComponent>(Owner, *FString::Printf(TEXT("GrabConstraint%s"), Side));
    GrabConstraint->RegisterComponent();
    GrabConstraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Free, 0.f);
    GrabConstraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Free, 0.f);
    GrabConstraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Free, 0.f);
    GrabConstraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0.f);
    GrabConstraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Free, 0.f);
    GrabConstraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.f);
    GrabConstraint->SetLinearPositionDrive(true, true, true);
    GrabConstraint->SetLinearVelocityDrive(true, true, true);
    GrabConstraint->SetLinearVelocityTarget(FVector::ZeroVector);
    GrabConstraint->SetLinearDriveAccelerationMode(false);
    GrabConstraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
    GrabConstraint->SetOrientationDriveSLERP(true);
    GrabConstraint->SetAngularVelocityDriveSLERP(true);
    GrabConstraint->SetAngularVelocityTarget(FVector::ZeroVector);
    GrabConstraint->SetAngularDriveAccelerationMode(false);
}

// ============================================================================
// 매 틱 — 앵커·드라이브·손 모양
// ============================================================================

void UVRHandComponent::UpdateTracking(float DeltaTime)
{
    if (!IsPhysicsActive() || !PalmConstraint) return;

    // 손 위치는 항상 실제 손(핸드트래킹 손바닥 관절)이고, 컨트롤러는 버튼만 쓴다 — 컨트롤러를 쥔 손도 런타임이 관절을 넘겨 준다
    // (MetaSimultaneousHands). 이 컴포넌트(앵커)가 곧 "진짜 손" 이라 드라이브·이펙터·순간이동이 전부 앵커를 읽고,
    // 관절 상태는 손가락 포즈(애님 인스턴스)·이펙터 손목 보정·접촉 쥐기가 같이 읽는다.
    // 트래킹이 끊기면(빠른 휘두르기·시야 밖) 마지막 관절 상태를 폰 기준으로 유지한다 — 몸을 따라오되 손 모양·쥔 물건은 그대로.
    // 한 번도 안 잡혔으면(헤드셋 없는 PIE 등) 앵커는 붙어 있는 모션 컨트롤러 Grip 포즈 그대로다.
    TrackState = FXRHandTrackingState();
    if (GEngine && GEngine->XRSystem.IsValid())
    {
        GEngine->XRSystem->GetHandTrackingState(this, EXRSpaceType::UnrealWorldSpace, Hand, TrackState);
    }
    TrackState.bValid = TrackState.bValid && TrackState.TrackingStatus == ETrackingStatus::Tracked
        && TrackState.HandKeyLocations.Num() == EHandKeypointCount && TrackState.HandKeyRotations.Num() == EHandKeypointCount;

    const FTransform OwnerTM = GetOwner()->GetActorTransform();
    if (TrackState.bValid)
    {
        LastTrackInOwner = TrackState;
        for (int32 k = 0; k < EHandKeypointCount; ++k)
        {
            LastTrackInOwner.HandKeyLocations[k] = OwnerTM.InverseTransformPosition(TrackState.HandKeyLocations[k]);
            LastTrackInOwner.HandKeyRotations[k] = OwnerTM.InverseTransformRotation(TrackState.HandKeyRotations[k]);
        }
    }
    else if (LastTrackInOwner.bValid)
    {
        TrackState = LastTrackInOwner;
        for (int32 k = 0; k < EHandKeypointCount; ++k)
        {
            TrackState.HandKeyLocations[k] = OwnerTM.TransformPosition(LastTrackInOwner.HandKeyLocations[k]);
            TrackState.HandKeyRotations[k] = OwnerTM.TransformRotation(LastTrackInOwner.HandKeyRotations[k]);
        }
    }

    const int32 PalmKey = static_cast<int32>(EHandKeypoint::Palm);
    if (TrackState.bValid)
    {
        SetWorldLocationAndRotation(TrackState.HandKeyLocations[PalmKey], TrackState.HandKeyRotations[PalmKey]);
    }

    // 물리 엔진은 NPC 뼈 캡슐을 모르므로 드라이브 목표를 뼈 캡슐 밖으로 옮겨 손이 표면에서 멈추게 한다.
    const FVector RealHand = GetComponentLocation();
    const FQuat TargetRotation = GetComponentQuat() * PalmShapeOffset(TrackState.bValid);
    ACombatCharacter* TouchedNPC = nullptr;
    const FVector Target = ProjectOutOfNPCs(RealHand, TargetRotation, TouchedNPC);
    PushTouchedNPC(TouchedNPC, Target, RealHand, DeltaTime);   // 드라이브 전 물리 손 위치로 "표면에 와 있나"를 본다
    UpdatePassThrough(DriveBody(Target, TargetRotation, DeltaTime, HeldMassScale()));
    UpdateFingertipShapes();

    // 쥐는 정도 평활화 — 애님의 직전 갱신 값(한 프레임 늦음)을 지수 평활.
    const UVRPawnAnimInstance* Anim = GetAnim();
    const float Alpha = SqueezeSmoothTime > KINDA_SMALL_NUMBER ? 1.f - FMath::Exp(-DeltaTime / SqueezeSmoothTime) : 1.f;
    for (int32 f = 0; f < 5; ++f)
    {
        const float Raw = Anim && TrackState.bValid ? Anim->GetFingerSqueeze(Hand, f) : 0.f;
        SmoothedSqueeze[f] += (Raw - SmoothedSqueeze[f]) * Alpha;
    }
}

float UVRHandComponent::DriveBody(const FVector& Location, const FQuat& Rotation, float DeltaTime, float MassScale)
{
    // 잠든 바디는 드라이브 목표가 바뀌어도 깨어나지 않는다 — 손을 잠시 멈췄다 움직이면 제자리에 남는다.
    if (!Palm->IsAnyRigidBodyAwake()) Palm->WakeAllRigidBodies();
    const float Error = FVector::Dist(Palm->GetComponentLocation(), Location);

    // 바디가 목표에서 이 거리(cm) 이상 떨어지면 드라이브로 못 따라온다고 보고 목표로 순간이동.
    // 대쉬(0.15초에 300cm)·리스폰·착석 스냅 때 손이 뒤에 남거나 벽 뒤에 끼는 것을 막는다.
    // ponytail: 벽 너머로 이 거리 이상 손을 밀어 넣으면 바디가 벽을 통과한다 — 막아야 하면 순간이동 전 스윕 검사 추가.
    static constexpr float SnapDistance = 60.f;
    if (Error > SnapDistance)
    {
        // ResetPhysics — 위치와 함께 잔류 속도도 비워 순간이동 직후 튕겨 나가지 않게 한다.
        Palm->SetWorldLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::ResetPhysics);
        bHasPrevTarget = false;
    }

    // 빈손·접촉·쥐기 모두 선형 드라이브(스프링) 하나 — 엔진이 접촉과 함께 풀어 벽·물건을 꾸준히 누른다.
    // 예전엔 빈손만 매 틱 속도를 덮어쓰고 닿으면 스프링으로 바꿨는데, 두 방식 경계에서 양손 맞대기가 0.14cm(2프레임 주기)
    // 떨렸다. 강성 22500·임계감쇠 300·가속도 상한 100m/s² 면 스프링만으로도 빈손 지연이 없다(상한 15m/s² 일 땐 2m/s 손이
    // 멈추는 데 13cm 더 가서 속도 덮어쓰기가 필요했다).
    //
    // 목표 속도 앞먹임 — 속도 드라이브(감쇠)가 목표 속도 0 을 향하면 움직이는 동안 감쇠×속도/강성 만큼 뒤처진다.
    // 막혀 있어도 끄지 않는다: 손이 멈추면 목표 속도도 0 이라 계속 밀지 않고, 미는 힘은 최대 힘으로 막힌다.
    // 트래킹 튐 대비 5m/s 상한.
    const FVector TargetVelocity = (bHasPrevTarget && DeltaTime > KINDA_SMALL_NUMBER ? (Location - PrevTarget) / DeltaTime : FVector::ZeroVector).GetClampedToMaxSize(500.f);
    PrevTarget = Location;
    bHasPrevTarget = true;

    // 튜닝 값을 PIE 중 Details 에서 바꿔도 바로 먹도록 매 틱 반영한다.
    // 드라이브는 가속도 모드라 힘 = 이 바디 질량 × 가속도 — 제약으로 매단 물건의 질량은 모른다.
    // 쥐고 있으면 MassScale 만큼 키워야 빈손과 같은 반응으로 따라오고 물건 무게를 받친다.
    PalmConstraint->SetLinearDriveParams(DriveStiffness * MassScale, DriveDamping * MassScale, DriveMaxForce * MassScale);
    PalmConstraint->SetLinearPositionTarget(Location - PalmConstraint->GetComponentLocation());
    PalmConstraint->SetLinearVelocityTarget(TargetVelocity);

    // ponytail: 회전은 스프링 그대로, 쥐면 같은 배율 — 긴 물건(관성 m·r²)은 실제보다 덜 무겁게 돈다. 필요하면 관성비로 따로.
    PalmConstraint->SetAngularDriveParams(AngularStiffness * MassScale, AngularDamping * MassScale, 0.f);
    PalmConstraint->SetAngularOrientationTarget(Rotation.Rotator());
    return Error;
}

float UVRHandComponent::HeldMassScale() const
{
    const ADroppedItemBase* Held = GrabbedItem.Get();
    if (!IsValid(Held) || !IsValid(Held->ItemMesh) || !IsValid(Palm)) return 1.f;
    const float PalmMass = FMath::Max(Palm->GetMass(), KINDA_SMALL_NUMBER);
    return (PalmMass + FMath::Min(Held->ItemMesh->GetMass(), MaxCarryMass)) / PalmMass;
}

void UVRHandComponent::UpdatePassThrough(float Error)
{
    // 실제 손은 트래킹상 서로 통과할 수 있지만 물리 손은 못 지나가, 양손이 반대편에 끼면 서로 밀며 멈춘다.
    // 목표에서 많이 밀려난 바디만 반대 손 충돌을 잠시 끄고, 절반 안으로 돌아오면 다시 켠다(벽 충돌은 유지).
    // 한쪽만 무시해도 충돌 응답은 둘 중 약한 쪽을 따르므로 끼임이 풀린다.
    const ECollisionChannel Other = IsLeft() ? ECC_HandRight : ECC_HandLeft;
    const bool bBlocking = Palm->GetCollisionResponseToChannel(Other) == ECR_Block;
    if (bBlocking && Error > PassThroughDistance)                Palm->SetCollisionResponseToChannel(Other, ECR_Ignore);
    else if (!bBlocking && Error < PassThroughDistance * 0.5f)  Palm->SetCollisionResponseToChannel(Other, ECR_Block);
}

void UVRHandComponent::PushTouchedNPC(ACombatCharacter* NPC, const FVector& Surface, const FVector& RealHand, float DeltaTime)
{
    // 실제 손이 NPC 뼈 캡슐 안으로 들어간 깊이(표면으로 옮긴 목표 ↔ 실제 손)에 비례한 속도로, 들어간 방향(수평)으로 민다.
    // 물리 손이 그 표면에 와 있을 때만 — 벽에 막혀 NPC 에 못 닿았으면 실제 손이 NPC 안에 있어도 밀지 않는다.
    // 스윙(폰의 TryMeleeHits)은 1m/s 이상 순간 타격이고, 이건 느리게 대고 미는 접촉 몫이다.
    static constexpr float SurfaceTolerance = 2.f;   // 물리 손 ↔ 표면 목표(cm). 빈손 드라이브는 목표를 1cm 안으로 따른다.
    if (!NPC || NPC->bIsDead || PushGain <= 0.f) return;
    if (FVector::DistSquared(Palm->GetComponentLocation(), Surface) > FMath::Square(SurfaceTolerance)) return;

    const float Depth = FVector::Dist(RealHand, Surface);
    FVector Dir = RealHand - Surface;
    Dir.Z = 0.f;
    if (Depth < PushMinError || !Dir.Normalize()) return;

    // 스윕으로 옮겨 벽·지형에 막히면 거기서 멈춘다.
    const float PushSpeed = FMath::Min((Depth - PushMinError) * PushGain, MaxPushSpeed);
    NPC->AddActorWorldOffset(Dir * PushSpeed * DeltaTime, /*bSweep=*/true);
}

FVector UVRHandComponent::ProjectOutOfNPCs(const FVector& Location, const FQuat& Rotation, ACombatCharacter*& OutTouched) const
{
    // 손 모양이 닿을 수 있는 NPC 의 뼈 캡슐만 모은다. 메시 경계는 포즈(애니메이션·래그돌)를 따라간다.
    // 뼈 캡슐 치수가 없는 NPC 는 GetWorldCapsules 가 거절하고, 그 NPC 는 몸 캡슐이 손을 막는다.
    static constexpr float HandReach = 30.f;   // 손바닥 바디 중심 → 손 모양 끝(손끝)까지 여유(cm)
    OutTouched = nullptr;
    TArray<FNPCWorldCapsule> Bones, Caps;
    TArray<ACombatCharacter*> BoneOwners;   // Bones 와 같은 순서
    for (TActorIterator<ACombatCharacter> It(GetWorld()); It; ++It)
    {
        const USkeletalMeshComponent* NPCMesh = It->GetMesh();
        if (!It->BoneCapsules || !NPCMesh) continue;
        if (FVector::DistSquared(NPCMesh->Bounds.Origin, Location) > FMath::Square(NPCMesh->Bounds.SphereRadius + HandReach)) continue;
        if (!It->BoneCapsules->GetWorldCapsules(NPCMesh, Caps)) continue;
        Bones.Append(Caps);
        while (BoneOwners.Num() < Bones.Num()) BoneOwners.Add(*It);
    }
    if (Bones.IsEmpty()) return Location;

    // 손 모양 = 손바닥 바디 기준 선분 + 반지름. 손바닥 상자마다 긴 축을 따라 짧은 축 양 끝에 캡슐 두 줄(반지름 = 두께 절반),
    // 손가락은 켜져 있을 때(핸드트래킹)만 캡슐 그대로.
    struct FHandSegment { FVector A, B; float Radius; };
    TArray<FHandSegment, TInlineAllocator<2 * UVRPawnAnimInstance::NumPalmShapes + UVRPawnAnimInstance::NumFingerShapes>> Segments;
    for (const UBoxComponent* Shape : PalmShapes)
    {
        if (!Shape) continue;
        const FTransform Rel = Shape->GetRelativeTransform();
        const FVector E = Shape->GetUnscaledBoxExtent();
        const float R = E.Z;
        const FVector Long = E.X >= E.Y ? FVector(FMath::Max(E.X - R, 0.f), 0.f, 0.f) : FVector(0.f, FMath::Max(E.Y - R, 0.f), 0.f);
        const FVector Side = E.X >= E.Y ? FVector(0.f, FMath::Max(E.Y - R, 0.f), 0.f) : FVector(FMath::Max(E.X - R, 0.f), 0.f, 0.f);
        Segments.Add({ Rel.TransformPosition(Side - Long), Rel.TransformPosition(Side + Long), R });
        Segments.Add({ Rel.TransformPosition(-Side - Long), Rel.TransformPosition(-Side + Long), R });
    }
    if (bFingertipsActive)
    {
        for (const UCapsuleComponent* Finger : FingerBodies)
        {
            if (!Finger) continue;
            const FTransform Rel = Finger->GetRelativeTransform();
            const FVector Half(0.f, 0.f, Finger->GetUnscaledCapsuleHalfHeight_WithoutHemisphere());
            Segments.Add({ Rel.TransformPosition(-Half), Rel.TransformPosition(Half), Finger->GetUnscaledCapsuleRadius() });
        }
    }

    // 가장 깊이 박힌 짝의 깊이만큼 목표를 밀어내고, 밀린 곳에서 다른 캡슐에 박혔을 수 있으니 몇 번 되풀이한다. 위치만 옮기고 회전은 그대로.
    // 밀어낼 방향은 지금 물리 손 쪽 — 실제 손이 뼈 축을 넘어 들어가도(몸통 반지름 ≈ 15cm) 반대편 표면으로 튀지 않는다.
    // 축을 넘은 실제 손은 축 기준으로 들어온 쪽에 비춰서 본다. 그대로 두면 목표가 표면을 따라 축 너머 쪽으로 계속 끌려
    // 손이 몸을 돌아 뒤로 미끄러진다(헤드셋 없는 PIE 실측: 몸통 축 10cm 너머에서 손이 앞 → 뒤 표면으로 돌아감).
    // 실제 손이 반대편 표면 밖까지 나가면 더 겹치지 않아 물리 손이 몸을 지나간다.
    const FTransform BodyTM = Palm->GetComponentTransform();
    FVector Target = Location;
    for (int32 Iter = 0; Iter < 4; ++Iter)
    {
        const FTransform TargetTM(Rotation, Target);
        float Depth = 0.f;
        FVector Push = FVector::ZeroVector;
        for (const FHandSegment& S : Segments)
        {
            const FVector A = TargetTM.TransformPosition(S.A);
            const FVector B = TargetTM.TransformPosition(S.B);
            for (int32 b = 0; b < Bones.Num(); ++b)
            {
                const FNPCWorldCapsule& C = Bones[b];
                FVector P, Q;
                FMath::SegmentDistToSegmentSafe(A, B, C.A, C.B, P, Q);
                const float Reach = S.Radius + C.Radius;
                if (FVector::DistSquared(P, Q) >= FMath::Square(Reach)) continue;

                FVector BodyP, BodyQ;
                FMath::SegmentDistToSegmentSafe(BodyTM.TransformPosition(S.A), BodyTM.TransformPosition(S.B), C.A, C.B, BodyP, BodyQ);
                FVector N = (BodyP - BodyQ).GetSafeNormal();
                if (N.IsZero()) N = (P - Q).GetSafeNormal();
                if (N.IsZero()) continue;   // 물리 손·목표 둘 다 뼈 축 위 — 방향을 모른다
                const FVector Gap = P - Q;
                const bool bCrossed = FVector::DotProduct(Gap, N) < 0.f;
                const float D = Reach - FMath::Abs(FVector::DotProduct(Gap, N));
                if (D > Depth)
                {
                    Depth = D;
                    Push = (bCrossed ? -2.f * Gap : FVector::ZeroVector) + N * D;
                    if (Iter == 0) OutTouched = BoneOwners[b];   // 실제 손 위치에서 가장 깊이 들어간 NPC
                }
            }
        }
        if (Depth <= KINDA_SMALL_NUMBER) break;
        Target += Push;
    }
    return Target;
}

void UVRHandComponent::ApplyColliderSizes()
{
    // 손 콜라이더 치수는 보이는 손 메시에 맞춘 값(애님 인스턴스) × 두께 배율. 배율을 PIE 중 바꾸면 다음 틱에 다시 적용.
    // 용접된 모양의 크기는 떼고 바꾼 뒤 다시 붙인다 — 붙은 채 바꾸면 손바닥 바디의 모양 목록에 반영된다는 보장이 없다.
    const UVRPawnAnimInstance* Anim = GetAnim();
    if (!Anim || !Anim->HasHandShapes() || AppliedColliderScale == ColliderRadiusScale) return;
    AppliedColliderScale = ColliderRadiusScale;

    for (UCapsuleComponent* Body : FingerBodies)
    {
        if (Body) Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }
    for (UBoxComponent* Shape : PalmShapes)
    {
        if (Shape) Shape->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }

    // 바디 자체는 손바닥 관절에 중심을 둔 1cm 핵 — 실제 손바닥 모양은 용접된 PalmShape 가 맡는다.
    Palm->SetBoxExtent(FVector(1.f));
    for (int32 Part = 0; Part < PalmShapes.Num(); ++Part)
    {
        UBoxComponent* Shape = PalmShapes[Part];
        if (!Shape) continue;
        FVector PalmExtent = Anim->GetPalmHalfExtent(Hand, Part);
        PalmExtent.Z *= ColliderRadiusScale;
        Shape->SetBoxExtent(PalmExtent);
        Shape->SetRelativeTransform(ComputePalmShapeRelative(Part));
        Shape->AttachToComponent(Palm, FAttachmentTransformRules(EAttachmentRule::KeepWorld, true));
    }

    for (int32 f = 0; f < 5; ++f)
    {
        for (int32 s = 0; s < UVRPawnAnimInstance::ShapesPerFinger; ++s)
        {
            const int32 i = UVRPawnAnimInstance::ShapeIndex(f, s);
            UCapsuleComponent* Body = FingerBodies.IsValidIndex(i) ? FingerBodies[i].Get() : nullptr;
            if (!Body) continue;
            const float Radius = Anim->GetShapeRadius(Hand, f, s) * ColliderRadiusScale;
            Body->SetCapsuleSize(Radius, FMath::Max(Anim->GetShapeHalfHeight(Hand, f, s), Radius));
            Body->AttachToComponent(Palm, FAttachmentTransformRules(EAttachmentRule::KeepWorld, true));
        }
    }
}

FTransform UVRHandComponent::ComputePalmShapeRelative(int32 Part) const
{
    // 손바닥 모양은 "손 본 원점(손목) + 손바닥 관절 축" 기준 중심(애님 인스턴스가 메시 정점으로 잰 값)에 놓인다.
    // 핸드트래킹: 바디 = 손바닥 관절, 손 본 원점 = 손목 관절 → 손바닥 관절 기준 손목 오프셋 + 중심, 축은 그대로.
    // 컨트롤러: 이펙터가 손 본을 바디 위치에 (바디 회전 × 모양 보정⁻¹ × 그립 보정) 으로 두므로 손바닥 관절 축은
    //          바디 기준 모양 보정⁻¹ × 그립 보정 × PalmToBone⁻¹.
    const UVRPawnAnimInstance* Anim = GetAnim();
    if (!Anim) return FTransform::Identity;
    const FVector Center = Anim->GetPalmBoxCenter(Hand, Part);
    if (TrackState.bValid)
    {
        const FQuat PalmInverse = TrackState.HandKeyRotations[static_cast<int32>(EHandKeypoint::Palm)].Inverse();
        const FVector WristOffset = PalmInverse.RotateVector(TrackState.HandKeyLocations[static_cast<int32>(EHandKeypoint::Wrist)]
                                                           - TrackState.HandKeyLocations[static_cast<int32>(EHandKeypoint::Palm)]);
        return FTransform(FQuat::Identity, WristOffset + Center);
    }
    const FQuat Axes = PalmShapeOffset(false).Inverse() * GripOffset.Quaternion() * Anim->GetPalmToHandBone(Hand).Inverse();
    return FTransform(Axes, Axes.RotateVector(Center));
}

void UVRHandComponent::UpdateFingertipShapes()
{
    if (FingerBodies.Num() != UVRPawnAnimInstance::NumFingerShapes) return;
    ApplyColliderSizes();
    const bool bLeft = IsLeft();

    // ponytail: 컨트롤러일 땐 관절이 없어 손끝 모양의 충돌을 끄고 손바닥 안에 둔다 — 컨트롤러 손가락도 막아야 하면 애니 포즈 손끝을 목표로.
    // 손바닥 모양 — 입력 소스가 바뀌거나(손바닥 축이 달라짐) 손목 오프셋이 변하면 다시 놓는다.
    // 양손 끼임 해소(UpdatePassThrough)는 손바닥에 걸리므로, 용접된 모양도 반대 손 응답을 손바닥과 맞춘다.
    const ECollisionChannel OtherHand = bLeft ? ECC_HandRight : ECC_HandLeft;
    const ECollisionResponse OtherResponse = Palm->GetCollisionResponseToChannel(OtherHand);
    for (int32 Part = 0; Part < PalmShapes.Num(); ++Part)
    {
        UBoxComponent* Shape = PalmShapes[Part];
        if (!Shape) continue;
        const FTransform Rel = ComputePalmShapeRelative(Part);
        if (FVector::DistSquared(Shape->GetRelativeLocation(), Rel.GetLocation()) > FMath::Square(0.2f)
            || Shape->GetRelativeRotation().Quaternion().AngularDistance(Rel.GetRotation()) > FMath::DegreesToRadians(2.f))
        {
            Shape->SetRelativeTransform(Rel);
        }
        if (Shape->GetCollisionResponseToChannel(OtherHand) != OtherResponse) Shape->SetCollisionResponseToChannel(OtherHand, OtherResponse);
    }

    const bool bTracked = TrackState.bValid;
    if (bTracked != bFingertipsActive)
    {
        bFingertipsActive = bTracked;
        for (UCapsuleComponent* Body : FingerBodies)
        {
            if (!Body) continue;
            if (bTracked) SetupHandBodyCollision(Body, bLeft);
            else
            {
                Body->SetCollisionResponseToAllChannels(ECR_Ignore);
                Body->SetRelativeLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
            }
        }
    }
    if (!bTracked) return;

    for (UCapsuleComponent* Body : FingerBodies)
    {
        if (Body && Body->GetCollisionResponseToChannel(OtherHand) != OtherResponse) Body->SetCollisionResponseToChannel(OtherHand, OtherResponse);
    }

    // 손가락 캡슐 = 보이는 메시 손가락의 가운데·끝마디 구간(애님 인스턴스가 트래킹 관절 + 본 길이로 손바닥 기준 FK).
    // 용접된 모양을 옮기면 엔진이 손바닥 바디의 모양을 다시 붙인다(UnWeld→Weld).
    // 손가락을 표면 안으로 굽히면 모양이 표면에 박히고, 엔진이 손 전체를 밀어내 손이 들린다.
    const UVRPawnAnimInstance* Anim = GetAnim();
    if (!Anim) return;
    for (int32 j = 0; j < FingerBodies.Num(); ++j)
    {
        UCapsuleComponent* Tip = FingerBodies[j];
        FVector Center;
        FQuat Rotation;
        if (!Tip || !Anim->GetFingerShapeTarget(Hand, j / UVRPawnAnimInstance::ShapesPerFinger, j % UVRPawnAnimInstance::ShapesPerFinger, Center, Rotation)) continue;
        // 목표는 손바닥 기준 상대값 — 손이 밀려도 변하지 않고 손가락이 실제로 움직일 때만 바뀐다.
        // 트래킹 미세 떨림(수 mm)마다 다시 붙이지 않도록 0.2cm·2° 미만 변화는 무시.
        if (FVector::DistSquared(Tip->GetRelativeLocation(), Center) < FMath::Square(0.2f)
            && Tip->GetRelativeRotation().Quaternion().AngularDistance(Rotation) < FMath::DegreesToRadians(2.f)) continue;
        Tip->SetRelativeLocationAndRotation(Center, Rotation);
    }
}

FTransform UVRHandComponent::GetEffectorCS(const USkeletalMeshComponent* BodyMesh) const
{
    // 손 위치·회전 모두 물리 손바닥(벽에 막힌 실제 자세). 손바닥 상자 회전에서 모양 보정을 빼면 "막힌 앵커" 회전이 된다.
    // 시뮬레이션 전(에디터·BeginPlay 이전)엔 앵커 그대로.
    if (!BodyMesh) return FTransform::Identity;
    FTransform W = GetComponentTransform();
    if (IsPhysicsActive())
    {
        W.SetLocation(Palm->GetComponentLocation());
        W.SetRotation(Palm->GetComponentQuat() * PalmShapeOffset(TrackState.bValid).Inverse());
    }

    // 핸드트래킹 중엔 앵커가 손바닥 중심 관절에 있는데 손 본 피벗은 손목이다 — 손목 관절까지의 차이만큼 당긴다.
    // 차이는 앵커 로컬로 옮겨 물리 손바닥 회전으로 다시 돌린다(벽에 막혀 손이 덜 돌았으면 손목도 그만큼).
    // 회전 보정도 컨트롤러 그립용이 아니라 손바닥 관절 프레임용(애님 인스턴스가 레퍼런스 포즈에서 계산)을 쓴다.
    FQuat Offset = GripOffset.Quaternion();
    const UVRPawnAnimInstance* Anim = Cast<UVRPawnAnimInstance>(BodyMesh->GetAnimInstance());
    // 애님의 프레임 내부 상태(이번 프레임 관절 복사 여부)에 기대면 호출 시점에 따라 손목 오프셋이 빠진다 — 트래킹 유효 여부만 본다.
    if (TrackState.bValid && Anim && Anim->HasHandShapes())
    {
        const FVector PalmToWrist = TrackState.HandKeyLocations[static_cast<int32>(EHandKeypoint::Wrist)]
                                  - TrackState.HandKeyLocations[static_cast<int32>(EHandKeypoint::Palm)];
        W.AddToTranslation(W.GetRotation() * (GetComponentQuat().Inverse() * PalmToWrist));
        Offset = Anim->GetPalmToHandBone(Hand);
    }

    FTransform T = W.GetRelativeTransform(BodyMesh->GetComponentTransform());
    // 축 보정을 손 로컬 공간에 적용(우측 곱) — 손이 회전해도 보정이 따라감.
    T.SetRotation(T.GetRotation() * Offset);
    return T;
}

// ============================================================================
// 제스처·접촉 쥐기
// ============================================================================

bool UVRHandComponent::UpdateGesture()
{
    // 슈미트 트리거 — 잡은 뒤엔 임계 + 여유를 넘어야 놓는다. 트래킹이 끊기면 제스처는 풀린다(그립이 없으면 놓는다).
    bool bNow = false;
    if (TrackState.bValid)
    {
        auto Dist = [this](EHandKeypoint A, EHandKeypoint B)
        {
            return FVector::Dist(TrackState.HandKeyLocations[static_cast<int32>(A)], TrackState.HandKeyLocations[static_cast<int32>(B)]);
        };
        const float Slack = bGesture ? PinchHysteresis : 0.f;
        const bool bPinch = Dist(EHandKeypoint::ThumbTip, EHandKeypoint::IndexTip) < PinchDistanceThreshold + Slack;
        // 주먹은 엄지·검지 위치가 사람마다 달라 중지·약지·새끼 끝이 모두 손바닥에 붙었는지만 본다.
        const float FistDist = FMath::Max3(Dist(EHandKeypoint::MiddleTip, EHandKeypoint::Palm),
                                           Dist(EHandKeypoint::RingTip,   EHandKeypoint::Palm),
                                           Dist(EHandKeypoint::LittleTip, EHandKeypoint::Palm));
        bNow = bPinch || FistDist < FistDistanceThreshold + Slack;
    }
    if (bNow == bGesture) return false;
    bGesture = bNow;
    return true;
}

bool UVRHandComponent::UpdateContactGrab(bool bAllowNewGrab)
{
    bool bHeld = false;
    if (bFingertipsActive && TrackState.bValid)
    {
        if (bContactHeld)
        {
            // 짝이 표면에서 떨어지면(손을 폄) 놓는다. 쥔 동안은 감싸기가 마디를 표면에 멈춰 두어 닿아 있다.
            // 핀치 = 엄지와 짝 손가락 둘 다, 주먹 = 손바닥과 손가락 하나 이상이 닿아 있어야 유지.
            const ADroppedItemBase* Item = ContactItem.Get();
            const UPrimitiveComponent* HeldMesh = IsValid(Item) ? Item->ItemMesh : nullptr;
            if (ContactPartner == PalmGripPartner)
            {
                bHeld = IsPalmTouching(HeldMesh, ContactReleaseMargin);
                bool bAnyFinger = false;
                for (int32 f = 1; f < 5 && bHeld && !bAnyFinger; ++f) bAnyFinger = FingerTouching(f, HeldMesh, ContactReleaseMargin) != nullptr;
                bHeld = bHeld && bAnyFinger;
            }
            else
            {
                bHeld = FingerTouching(0, HeldMesh, ContactReleaseMargin) && FingerTouching(ContactPartner, HeldMesh, ContactReleaseMargin);
            }
        }
        else if (bAllowNewGrab)
        {
            // 사이에 끼었어도 마찰 한계가 무게보다 작으면(약하게 쥠) 아직 안 쥔다 — 닿은 순간은 쥐는 힘이 0 이고,
            // 손가락을 더 오므리면 다음 틱들에 힘이 붙어 쥐어진다.
            int32 Partner = INDEX_NONE;
            ADroppedItemBase* Item = FindGraspedItem(Partner);
            float Force = 0.f, Torque = 0.f;
            FVector Center;
            if (Item) ComputeGrip(Item, /*bContact=*/true, Partner, Force, Torque, Center);
            if (Item && Force >= Item->ItemMesh->GetMass() * FMath::Abs(GetWorld()->GetGravityZ()))
            {
                ContactItem = Item;
                ContactPartner = Partner;
                bHeld = true;
            }
        }
    }
    if (bHeld == bContactHeld) return false;
    bContactHeld = bHeld;
    if (!bHeld)
    {
        ContactItem.Reset();
        ContactPartner = INDEX_NONE;
    }
    return true;
}

const UCapsuleComponent* UVRHandComponent::FingerTouching(int32 Finger, const UPrimitiveComponent* Item, float Margin) const
{
    if (!IsValid(Item) || Finger < 0 || Finger >= 5) return nullptr;
    for (int32 Shape = UVRPawnAnimInstance::ShapesPerFinger - 1; Shape >= 0; --Shape)   // 끝마디 먼저
    {
        const int32 i = UVRPawnAnimInstance::ShapeIndex(Finger, Shape);
        const UCapsuleComponent* Body = FingerBodies.IsValidIndex(i) ? FingerBodies[i].Get() : nullptr;
        if (Body && Item->OverlapComponent(Body->GetComponentLocation(), Body->GetComponentQuat(),
                                           FCollisionShape::MakeCapsule(Body->GetScaledCapsuleRadius() + Margin, Body->GetScaledCapsuleHalfHeight() + Margin)))
        {
            return Body;
        }
    }
    return nullptr;
}

bool UVRHandComponent::IsPalmTouching(const UPrimitiveComponent* Item, float Margin) const
{
    if (!IsValid(Item)) return false;
    for (const UBoxComponent* Shape : PalmShapes)
    {
        if (Shape && Item->OverlapComponent(Shape->GetComponentLocation(), Shape->GetComponentQuat(), FCollisionShape::MakeBox(Shape->GetScaledBoxExtent() + FVector(Margin))))
        {
            return true;
        }
    }
    return false;
}

bool UVRHandComponent::GetPalmShapeCenter(FVector& OutCenter) const
{
    OutCenter = FVector::ZeroVector;
    if (PalmShapes.IsEmpty()) return false;
    for (const UBoxComponent* Shape : PalmShapes)
    {
        if (!Shape) return false;
        OutCenter += Shape->GetComponentLocation() / PalmShapes.Num();
    }
    return true;
}

ADroppedItemBase* UVRHandComponent::FindGraspedItem(int32& OutPartner) const
{
    FVector PalmCenter;
    UWorld* World = GetWorld();
    if (!GetPalmShapeCenter(PalmCenter) || !World) return nullptr;
    const UInventoryComponent* Inventory = GetInventory();

    // 손 근처(손바닥 중심 20cm) 드랍 아이템이 후보. 두 쥐기 중 하나라도 맞으면 쥔다 —
    //   핀치: 엄지와 다른 손가락이 같은 아이템에 닿고, 두 캡슐을 잇는 선이 아이템을 지난다.
    //   주먹: 손바닥 모양이 닿고, 손가락 2개 이상이 닿으며, 손바닥 중심 → 그 캡슐 선이 아이템을 지난다.
    //         주먹으로 감싸면 엄지가 물건이 아니라 손가락 위를 덮어 핀치로는 안 걸린다.
    // "선이 지난다" = 사이에 끼었다. 같은 면을 나란히 누르는(밀기) 경우는 선이 표면 위로 지나가 걸리지 않는다.
    // ponytail: 손바닥에 올려 받치기(손가락이 안 감쌈)는 안 걸린다.
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, PalmCenter, FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody), FCollisionShape::MakeSphere(20.f));
    const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GraspTrace), false);
    for (const FOverlapResult& Overlap : Overlaps)
    {
        ADroppedItemBase* Item = Cast<ADroppedItemBase>(Overlap.GetActor());
        if (!Item || Overlap.GetComponent() != Item->ItemMesh) continue;
        if (Inventory && (Inventory->GetHeldItem(EEquipmentSlot::MainHand) == Item || Inventory->GetHeldItem(EEquipmentSlot::OffHand) == Item)) continue;
        auto Between = [&](const FVector& A, const FVector& B)
        {
            FHitResult Hit;
            return Item->ItemMesh->LineTraceComponent(Hit, A, B, TraceParams);
        };

        if (const UCapsuleComponent* Thumb = FingerTouching(0, Item->ItemMesh, ContactGrabMargin))
        {
            for (int32 f = 1; f < 5; ++f)
            {
                const UCapsuleComponent* Other = FingerTouching(f, Item->ItemMesh, ContactGrabMargin);
                if (Other && Between(Thumb->GetComponentLocation(), Other->GetComponentLocation()))
                {
                    OutPartner = f;
                    return Item;
                }
            }
        }
        if (IsPalmTouching(Item->ItemMesh, ContactGrabMargin))
        {
            int32 Wrapped = 0;
            for (int32 f = 1; f < 5; ++f)
            {
                const UCapsuleComponent* Finger = FingerTouching(f, Item->ItemMesh, ContactGrabMargin);
                if (Finger && Between(PalmCenter, Finger->GetComponentLocation())) ++Wrapped;
            }
            if (Wrapped >= 2)
            {
                OutPartner = PalmGripPartner;
                return Item;
            }
        }
    }
    return nullptr;
}

void UVRHandComponent::GetNearbyItemMeshes(TArray<const UPrimitiveComponent*, TInlineAllocator<4>>& Out) const
{
    FVector PalmCenter;
    UWorld* World = GetWorld();
    if (!GetPalmShapeCenter(PalmCenter) || !World) return;
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, PalmCenter, FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody), FCollisionShape::MakeSphere(20.f));
    for (const FOverlapResult& Overlap : Overlaps)
    {
        const ADroppedItemBase* Item = Cast<ADroppedItemBase>(Overlap.GetActor());
        if (Item && Overlap.GetComponent() == Item->ItemMesh) Out.AddUnique(Item->ItemMesh);
    }
}

// ============================================================================
// 물리 쥐기 제약
// ============================================================================

void UVRHandComponent::ComputeGrip(const ADroppedItemBase* Item, bool bContact, int32 Partner, float& OutForce, float& OutTorque, FVector& OutCenter) const
{
    OutForce = OutTorque = 0.f;
    OutCenter = Palm ? Palm->GetComponentLocation() : GetComponentLocation();
    if (!IsValid(Item) || !IsValid(Item->ItemMesh)) return;

    // μ = 아이템 물리 머티리얼 마찰. 머티리얼을 안 정한 아이템은 엔진 기본(0.7).
    const FBodyInstance* Body = Item->ItemMesh->GetBodyInstance();
    const UPhysicalMaterial* Material = Body ? Body->GetSimplePhysicalMaterial() : nullptr;
    const float Mu = Material ? Material->Friction : 0.7f;

    if (!bContact)
    {
        // 컨트롤러 — 손가락 접촉이 없으니 손바닥 중심에서 주먹으로 감싸 쥔 것으로 본다.
        FVector PalmCenter;
        if (GetPalmShapeCenter(PalmCenter)) OutCenter = PalmCenter;
        const float SumN = ControllerGripForce * GripValue;
        OutForce = Mu * SumN;
        OutTorque = Mu * SumN * ControllerGripRadius;
        return;
    }

    // 핸드트래킹 — 표면에 닿은 손가락마다 N = 쥐는 정도 × 강성, 접촉점 = 닿은 캡슐 중심.
    // 주먹(손바닥 짝)이면 손바닥이 손가락들의 반작용(엄지 뺀 ΣN)을 받는다.
    // 쥔 중심 = 접촉점들의 가운데(힘 가중 아님 — 닿은 순간엔 N 이 0 이라 정할 수 없다). 핀치면 두 손끝 사이라 그 점을 축으로 돈다.
    // 회전 팔 rᵢ = 중심 ↔ 접촉점, 최소 FingerPadRadius — 접촉 분포가 넓을수록(주먹) 덜 돈다. 쿨롱 마찰이라 면적은 힘 크기엔 안 들어간다.
    const UPrimitiveComponent* Mesh = Item->ItemMesh;
    TArray<TPair<FVector, float>, TInlineAllocator<6>> Contacts;
    float FingerN = 0.f;
    for (int32 f = 0; f < 5; ++f)
    {
        if (const UCapsuleComponent* Finger = FingerTouching(f, Mesh, ContactReleaseMargin))
        {
            const float N = SqueezeStiffness * SmoothedSqueeze[f];
            Contacts.Add({ Finger->GetComponentLocation(), N });
            if (f > 0) FingerN += N;
        }
    }
    FVector PalmCenter;
    if (Partner == PalmGripPartner && GetPalmShapeCenter(PalmCenter) && IsPalmTouching(Mesh, ContactReleaseMargin))
    {
        Contacts.Add({ PalmCenter, FingerN });
    }
    if (Contacts.IsEmpty()) return;

    OutCenter = FVector::ZeroVector;
    for (const TPair<FVector, float>& C : Contacts) OutCenter += C.Key / Contacts.Num();
    float SumN = 0.f, SumNR = 0.f;
    for (const TPair<FVector, float>& C : Contacts)
    {
        SumN += C.Value;
        SumNR += C.Value * FMath::Max(FVector::Dist(C.Key, OutCenter), FingerPadRadius);
    }
    OutForce = Mu * SumN;
    OutTorque = Mu * SumNR;
}

bool UVRHandComponent::CanHold(const ADroppedItemBase* Item) const
{
    if (!IsValid(Item) || !IsValid(Item->ItemMesh) || !GetWorld()) return false;
    const bool bContact = bContactHeld && ContactItem.Get() == Item;
    float Force = 0.f, Torque = 0.f;
    FVector Center;
    ComputeGrip(Item, bContact, ContactPartner, Force, Torque, Center);
    return Force >= Item->ItemMesh->GetMass() * FMath::Abs(GetWorld()->GetGravityZ());
}

bool UVRHandComponent::GrabWithPhysics(ADroppedItemBase* Item, bool bKeepCollision)
{
    UInventoryComponent* Inventory = GetInventory();
    if (!IsValid(Item) || !IsValid(Item->ItemMesh) || !IsPhysicsActive() || !IsValid(GrabConstraint) || !Inventory)
    {
        return false;
    }

    // 진열품은 구매 직후라 물리가 꺼져 있을 수 있다 — 제약은 시뮬레이션 바디끼리만 걸린다.
    if (!Item->ItemMesh->IsSimulatingPhysics()) Item->SetPhysicsFrozen(false);

    // 제약 기준점 = 쥔 중심(핀치면 두 손끝 사이, 주먹·컨트롤러면 손바닥) — 아이템은 그 점을 축으로 무게 토크만큼 돈다.
    bGrabContact = bContactHeld && ContactItem.Get() == Item;
    GrabPartner = bGrabContact ? ContactPartner : INDEX_NONE;
    float Force = 0.f, Torque = 0.f;
    FVector Center;
    ComputeGrip(Item, bGrabContact, GrabPartner, Force, Torque, Center);

    // 드라이브 강성·감쇠 — 질량(회전은 쥔 점 기준 관성 = 무게중심 관성 최대축 + m·d²) × ω², 임계감쇠 2·질량·ω.
    // ponytail: 관성은 최대축 하나로 — 길쭉한 물건의 긴 축 회전은 실제보다 단단하다. 축별로 달라야 하면 관성 텐서를 제약 축으로 돌려 쓸 것.
    const float Mass = Item->ItemMesh->GetMass();
    const float Inertia = Item->ItemMesh->GetInertiaTensor().GetMax() + Mass * FVector::DistSquared(Center, Item->ItemMesh->GetCenterOfMass());
    GrabLinStiffness = Mass * FMath::Square(GripLinearFrequency);
    GrabLinDamping = 2.f * Mass * GripLinearFrequency;
    GrabAngStiffness = Inertia * FMath::Square(GripAngularFrequency);
    GrabAngDamping = 2.f * Inertia * GripAngularFrequency;
    GrabLinTarget = FVector::ZeroVector;
    GrabAngTarget = FQuat::Identity;

    // MaxForce 0 은 엔진에서 "무제한" — 쥐는 힘이 0 이어도 최소 1 로 둔다.
    GrabConstraint->SetLinearDriveParams(GrabLinStiffness, GrabLinDamping, FMath::Max(Force, 1.f));
    GrabConstraint->SetAngularDriveParams(GrabAngStiffness, GrabAngDamping, FMath::Max(Torque, 1.f));
    GrabConstraint->SetLinearPositionTarget(FVector::ZeroVector);
    GrabConstraint->SetAngularOrientationTarget(FRotator::ZeroRotator);
    const FTransform Frame(Palm->GetComponentQuat(), Center);
    GrabConstraint->SetWorldLocationAndRotation(Frame.GetLocation(), Frame.GetRotation());
    GrabConstraint->SetDisableCollision(!bKeepCollision);
    GrabConstraint->SetConstrainedComponents(Palm, NAME_None, Item->ItemMesh, NAME_None);
    GrabFrameInPalm = Frame.GetRelativeTransform(Palm->GetComponentTransform());
    GrabFrameInItem = Frame.GetRelativeTransform(Item->ItemMesh->GetComponentTransform());

    // 튜닝용 — 쥘 때 물리 손이 실제 손에서 밀려난 거리, 질량, 쥔 점~무게중심 거리(토크 팔), 마찰 한계 대 무게.
    UE_LOG(LogTemp, Log, TEXT("[VRHand] 쥠 상세: %s %s 손 어긋남 %.1fcm, 질량 %.2fkg, 무게중심 거리 %.1fcm, 힘 한계 %.0f(무게 %.0f), 토크 한계 %.0f, 충돌 %s"),
           *Item->ItemData.ItemTemplateID, bGrabContact ? TEXT("접촉") : TEXT("컨트롤러"), FVector::Dist(GetComponentLocation(), Palm->GetComponentLocation()),
           Mass, FVector::Dist(Center, Item->ItemMesh->GetCenterOfMass()), Force, Mass * FMath::Abs(GetWorld()->GetGravityZ()), Torque,
           bKeepCollision ? TEXT("유지") : TEXT("끔"));

    Inventory->HoldItem(Item, GetHandSlot());
    GrabbedItem = Item;
    return true;
}

void UVRHandComponent::ReleaseGrab()
{
    GrabbedItem.Reset();
    if (IsValid(GrabConstraint))
    {
        GrabConstraint->BreakConstraint();
    }
}

void UVRHandComponent::SyncGrab()
{
    const UInventoryComponent* Inventory = GetInventory();
    if (Inventory && GrabbedItem.IsValid() && Inventory->GetHeldItem(GetHandSlot()) != GrabbedItem.Get()) ReleaseGrab();
    const ADroppedItemBase* Item = GrabbedItem.Get();
    if (!IsValid(Item) || !IsValid(Item->ItemMesh) || !IsPhysicsActive()) return;

    // 마찰 한계 갱신 — 쥐는 힘을 빼면 한계가 내려가 물건이 스르륵 빠진다.
    // ponytail: 엔진 상한은 축별(X·Y·Z) 클램프라 대각 방향은 최대 √3 배까지 버틴다. 원뿔이 필요하면 상한을 축 분해해 넣을 것.
    float Force = 0.f, Torque = 0.f;
    FVector Center;
    ComputeGrip(Item, bGrabContact, GrabPartner, Force, Torque, Center);
    Force = FMath::Max(Force, 1.f);
    Torque = FMath::Max(Torque, 1.f);
    GrabConstraint->SetLinearDriveParams(GrabLinStiffness, GrabLinDamping, Force);
    GrabConstraint->SetAngularDriveParams(GrabAngStiffness, GrabAngDamping, Torque);

    // 재고착(정지마찰) — 드라이브가 한계에 걸려 밀린 만큼 목표를 현재 자세 쪽으로 옮겨 스프링 늘어남을 한계/강성으로 유지한다.
    // 안 하면 하중이 줄 때 스프링이 원래 자세로 튕겨 출렁인다(PIE 스파이크: 재고착 없으면 23°↔90° 진동).
    // 늘어남 한계는 엔진 실효 강성 기준 — Chaos 는 회전 드라이브 강성에 1.5 배(p.Chaos.JointConstraint.AngularDriveStiffnessScale)를 곱한다.
    // 넣은 강성으로 나누면 스프링이 늘 한계의 1.5 배로 포화돼 감쇠 몫이 없고, 평형각 둘레로 감쇠 없이 출렁인다(PIE: 횃불 끝 쥐기 10°↔49°).
    // 지금 자세 = 아이템 쪽 프레임 기준 손바닥 쪽 프레임 — 드라이브 목표 좌표. 반대(손바닥 기준 아이템)로 넣으면 목표가 거울 위치에 놓여
    // 드라이브가 운동 방향과 무관하게 한쪽으로만 상한만큼 밀어, 평형각 둘레로 감쇠 없이 출렁인다(PIE: 횃불 끝 쥐기 0°↔50°).
    const FTransform PalmFrame = GrabFrameInPalm * Palm->GetComponentTransform();
    const FTransform ItemFrame = GrabFrameInItem * Item->ItemMesh->GetComponentTransform();
    const FTransform Rel = PalmFrame.GetRelativeTransform(ItemFrame);

    const FVector Stretch = GrabLinTarget - Rel.GetLocation();
    const float MaxStretch = Force / FMath::Max(GrabLinStiffness, KINDA_SMALL_NUMBER);
    if (Stretch.Size() > MaxStretch)
    {
        GrabLinTarget = Rel.GetLocation() + Stretch.GetSafeNormal() * MaxStretch;
        GrabConstraint->SetLinearPositionTarget(GrabLinTarget);
    }
    const float Twist = Rel.GetRotation().AngularDistance(GrabAngTarget);
    const float MaxTwist = Torque / FMath::Max(GrabAngStiffness * AngularDriveStiffnessScale(), KINDA_SMALL_NUMBER);
    if (Twist > MaxTwist)
    {
        GrabAngTarget = FQuat::Slerp(Rel.GetRotation(), GrabAngTarget, MaxTwist / Twist).GetNormalized();
        GrabConstraint->SetAngularOrientationTarget(GrabAngTarget.Rotator());
    }

    // 쥔 점이 손에서 너무 멀리 미끄러졌으면 놓쳤다(제약은 상한만 있어 스스로 끊기지 않는다).
    if (Rel.GetLocation().Size() > GripSlipReleaseDistance) DropSlipped();
}

void UVRHandComponent::DropSlipped()
{
    UInventoryComponent* Inventory = GetInventory();
    ReleaseGrab();
    // 던지기·건네기 분기 없이 그 자리에서 떨어진다(ReleaseHeldItem 이 물리를 되살린다).
    if (ADroppedItemBase* Dropped = Inventory ? Inventory->ReleaseHeldItem(GetHandSlot()) : nullptr)
    {
        UE_LOG(LogTemp, Log, TEXT("[VRHand] 놓침(미끄러짐): %s"), *Dropped->ItemData.ItemTemplateID);
    }
}
