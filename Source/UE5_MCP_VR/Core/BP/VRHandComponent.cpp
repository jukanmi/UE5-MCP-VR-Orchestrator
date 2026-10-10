#include "Core/BP/VRHandComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/BP/VRPawnAnimInstance.h"
#include "Core/Types/CollisionChannels.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "IXRTrackingSystem.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Components/InventoryComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

// VR 손 — 생성·트래킹·드라이브·손 모양 배치. 쥐기는 VRHandGrab.cpp, NPC 밀어내기·밀기는 VRHandNPC.cpp.

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

    // 이동·회전 모두 자유인 제약 — 드라이브만이 바디를 목표 쪽으로 당긴다(손바닥 ↔ 월드, 손바닥 ↔ 쥔 물건 공용).
    // 강성은 위치 드라이브, 감쇠는 속도 드라이브에 걸리므로 둘 다 켠다. 목표 속도 0.
    // 회전은 SLERP 한 축으로 — 스윙/트위스트 분리 드라이브는 큰 회전에서 짐벌처럼 꺾인다.
    UPhysicsConstraintComponent* MakeDriveConstraint(AActor* Owner, const FString& Name)
    {
        UPhysicsConstraintComponent* C = NewObject<UPhysicsConstraintComponent>(Owner, *Name);
        C->RegisterComponent();
        C->SetLinearXLimit(ELinearConstraintMotion::LCM_Free, 0.f);
        C->SetLinearYLimit(ELinearConstraintMotion::LCM_Free, 0.f);
        C->SetLinearZLimit(ELinearConstraintMotion::LCM_Free, 0.f);
        C->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0.f);
        C->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Free, 0.f);
        C->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.f);
        C->SetLinearPositionDrive(true, true, true);
        C->SetLinearVelocityDrive(true, true, true);
        C->SetLinearVelocityTarget(FVector::ZeroVector);
        C->SetAngularDriveMode(EAngularDriveMode::SLERP);
        C->SetOrientationDriveSLERP(true);
        C->SetAngularVelocityDriveSLERP(true);
        C->SetAngularVelocityTarget(FVector::ZeroVector);
        return C;
    }
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
    const TCHAR* Side = IsLeft() ? TEXT("Left") : TEXT("Right");

    CreatePalm(Owner, Side);
    CreateHandShapes(Owner, Side);

    // 물리 쥐기 제약 — 잡을 때 손 바디와 아이템을 쥔 중심에서 잇는다. 드라이브가 쥔 자세로 당기되
    // 드라이브 최대 힘·토크를 마찰 한계로 둔다 — 넘치는 만큼 미끄러지거나 돈다(SyncGrab 이 매 틱 한계·목표 갱신).
    // 힘 모드(가속도 모드 끔) — 가속도 모드면 상한이 역질량으로 나뉘어 μ·N 이 힘이 아니게 된다.
    // 회전 토크 상한은 DefaultEngine.ini 의 p.Chaos.Solver.Joint.UseSimd=0 이어야 먹는다(엔진 SIMD 경로가 클램프 생략).
    // 손 ↔ 쥔 물건 충돌 여부는 쥘 때 방식별로 정한다.
    GrabConstraint = MakeDriveConstraint(Owner, FString::Printf(TEXT("GrabConstraint%s"), Side));
    GrabConstraint->SetLinearDriveAccelerationMode(false);
    GrabConstraint->SetAngularDriveAccelerationMode(false);
}

void UVRHandComponent::CreatePalm(AActor* Owner, const TCHAR* Side)
{
    // 물리 손바닥 — 바디(손바닥 관절에 중심). 실제 치수는 ApplyColliderSizes 가 넣는다.
    // 빠른 손놀림에 얇은 벽·지형을 건너뛰지 않게. CCD 가 아니라 MACD — 양손 모두 CCD 면 맞닿을 때 한 프레임은 충돌 순간으로
    // 되감고 다음 프레임은 파고들었다 튕겨 나와 2프레임 주기로 떨었다(PIE 실측 0.56cm/프레임 → MACD 0).
    Palm = NewObject<UBoxComponent>(Owner, *FString::Printf(TEXT("PhysicsPalm%s"), Side));
    Palm->InitBoxExtent(FVector(4.5f, 4.25f, 1.5f));
    SetupHandBodyCollision(Palm, IsLeft());
    Palm->SetUseMACD(true);
    Palm->SetWorldLocationAndRotation(GetComponentLocation(), FQuat::Identity);
    Palm->RegisterComponent();
    Palm->SetSimulatePhysics(true);

    // 제약은 손바닥 ↔ 월드. 드라이브 강성·목표는 매 틱 DriveBody 가 넣는다.
    // 제약 프레임을 월드 축 정렬로 바디 중심에 두고 마지막에 연결한다. 월드 쪽 프레임은 이 순간의
    // 제약 월드 변환으로 고정되므로, 위치 목표 = (목표 월드 위치 − 제약 월드 위치), 회전 목표 = 목표 월드 회전.
    PalmConstraint = MakeDriveConstraint(Owner, FString::Printf(TEXT("PalmConstraint%s"), Side));
    PalmConstraint->SetUsingAbsoluteLocation(true);
    PalmConstraint->SetUsingAbsoluteRotation(true);
    PalmConstraint->SetWorldLocationAndRotation(Palm->GetComponentLocation(), FQuat::Identity);
    PalmConstraint->SetConstrainedComponents(Palm, NAME_None, nullptr, NAME_None);
}

void UVRHandComponent::CreateHandShapes(AActor* Owner, const TCHAR* Side)
{
    const bool bLeft = IsLeft();

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
    const ADroppedItemBase* Held = Grab.Item.Get();
    if (!IsValid(Held) || !IsValid(Held->ItemMesh) || !IsValid(Palm)) return 1.f;
    const float PalmMass = FMath::Max(Palm->GetMass(), KINDA_SMALL_NUMBER);
    // 양손으로 쥔 물건은 무게를 두 손이 나눠 받는다.
    const UVRHandComponent* Other = GetOtherHand();
    const float Share = Other && Other->Grab.Item.Get() == Held ? 0.5f : 1.f;
    return (PalmMass + FMath::Min(Held->ItemMesh->CalculateMass(), MaxCarryMass) * Share) / PalmMass;
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
