#include "Player/Components/VRHandComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

#include "Core/Physics/CapsuleContact.h"
#include "Core/Physics/NPCBoneCapsuleSet.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Player/BP/VRPawnAnimInstance.h"

namespace
{
    // Chaos 가 회전 드라이브 강성에 곱하는 배율(엔진 기본 1.5). 콘솔 변수가 없으면 1.
    float AngularDriveStiffnessScale()
    {
        static const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveStiffnessScale"));
        return CVar ? CVar->GetFloat() : 1.f;
    }

    // 접촉 쥐기 짝 값 — 1~4 = 엄지의 짝 손가락(핀치), 이 값 = 손바닥과 손가락들로 감싸 쥠(주먹).
    constexpr int32 PalmGripPartner = 5;
}

// ============================================================================
// 제스처·접촉 쥐기
// ============================================================================

bool UVRHandComponent::UpdateGesture()
{
    // 슈미트 트리거 — 잡은 뒤엔 임계 + 여유를 넘어야 놓는다. 트래킹이 끊기면 제스처는 풀린다(그립이 없으면 놓는다).
    bool bGestureDetected = false;
    if (TrackState.bValid)
    {
        auto KeyDistance = [this](EHandKeypoint A, EHandKeypoint B)
        {
            return FVector::Dist(TrackState.HandKeyLocations[static_cast<int32>(A)], TrackState.HandKeyLocations[static_cast<int32>(B)]);
        };
        const float ReleaseSlack = bGesture ? PinchHysteresis : 0.f;
        const bool bPinch = KeyDistance(EHandKeypoint::ThumbTip, EHandKeypoint::IndexTip) < PinchDistanceThreshold + ReleaseSlack;
        // 주먹은 엄지·검지 위치가 사람마다 달라 중지·약지·새끼 끝이 모두 손바닥에 붙었는지만 본다.
        const float FarthestTipToPalm = FMath::Max3(KeyDistance(EHandKeypoint::MiddleTip, EHandKeypoint::Palm),
                                           KeyDistance(EHandKeypoint::RingTip,   EHandKeypoint::Palm),
                                           KeyDistance(EHandKeypoint::LittleTip, EHandKeypoint::Palm));
        bGestureDetected = bPinch || FarthestTipToPalm < FistDistanceThreshold + ReleaseSlack;
    }
    if (bGestureDetected == bGesture) return false;
    bGesture = bGestureDetected;
    return true;
}

bool UVRHandComponent::UpdateContactGrab(bool bAllowNewGrab)
{
    bool bHolding = false;
    if (bFingertipsActive && TrackState.bValid)
    {
        if (Contact.bHeld)
        {
            // 짝이 표면에서 떨어지면(손을 폄) 놓는다. 쥔 동안은 감싸기가 마디를 표면에 멈춰 두어 닿아 있다.
            // 핀치 = 엄지와 짝 손가락 둘 다, 주먹 = 손바닥과 손가락 하나 이상이 닿아 있어야 유지.
            const ADroppedItemBase* Item = Contact.Item.Get();
            const UPrimitiveComponent* HeldMesh = IsValid(Item) ? Item->ItemMesh : nullptr;
            const FFingerTouches Touched = TouchingFingers(HeldMesh, ContactReleaseMargin);
            if (Contact.Partner == PalmGripPartner)
            {
                bHolding = IsPalmTouching(HeldMesh, ContactReleaseMargin) && (Touched[1] || Touched[2] || Touched[3] || Touched[4]);
            }
            else
            {
                bHolding = Touched[0] && Touched[Contact.Partner];
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
            if (Item && Force + GetOtherHandGripForce(Item) >= Item->ItemMesh->CalculateMass() * FMath::Abs(GetWorld()->GetGravityZ()))
            {
                Contact.Item = Item;
                Contact.Partner = Partner;
                bHolding = true;
            }
        }
    }
    if (bHolding == Contact.bHeld) return false;
    Contact.bHeld = bHolding;
    if (!bHolding) Contact = FContactGrab();
    return true;
}

UVRHandComponent::FFingerTouches UVRHandComponent::TouchingFingers(const UPrimitiveComponent* Item, float Margin) const
{
    FFingerTouches Touched(InPlace, nullptr);
    if (!IsValid(Item)) return Touched;
    for (int32 Finger = 0; Finger < 5; ++Finger)
    {
        for (int32 Shape = UVRPawnAnimInstance::ShapesPerFinger - 1; Shape >= 0 && !Touched[Finger]; --Shape)   // 끝마디 먼저
        {
            const int32 ShapeIdx = UVRPawnAnimInstance::ShapeIndex(Finger, Shape);
            const UCapsuleComponent* Capsule = FingerBodies.IsValidIndex(ShapeIdx) ? FingerBodies[ShapeIdx].Get() : nullptr;
            if (Capsule && Item->OverlapComponent(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(),
                                               FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() + Margin, Capsule->GetScaledCapsuleHalfHeight() + Margin)))
            {
                Touched[Finger] = Capsule;
            }
        }
    }
    return Touched;
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

    // 손 근처(손바닥 중심 20cm) 드랍 아이템이 후보. 두 쥐기 중 하나라도 맞으면 쥔다 — 판정은 IsItemGrasped.
    // ponytail: 손바닥에 올려 받치기(손가락이 안 감쌈)는 안 걸린다.
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, PalmCenter, FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody), FCollisionShape::MakeSphere(20.f));
    for (const FOverlapResult& Overlap : Overlaps)
    {
        ADroppedItemBase* Item = Cast<ADroppedItemBase>(Overlap.GetActor());
        if (!Item || Overlap.GetComponent() != Item->ItemMesh) continue;
        if (Grab.Item.Get() == Item) continue;   // 다른 손이 쥔 물건은 후보다(양손 쥐기)
        if (IsItemGrasped(*Item, PalmCenter, OutPartner)) return Item;
    }
    return nullptr;
}

bool UVRHandComponent::IsItemGrasped(const ADroppedItemBase& Item, const FVector& PalmCenter, int32& OutPartner) const
{
    //   핀치: 엄지와 다른 손가락이 같은 아이템에 닿고, 두 캡슐을 잇는 선이 아이템을 지난다.
    //   주먹: 손바닥 모양이 닿고, 손가락 2개 이상이 닿으며, 손바닥 중심 → 그 캡슐 선이 아이템을 지난다.
    //         주먹으로 감싸면 엄지가 물건이 아니라 손가락 위를 덮어 핀치로는 안 걸린다.
    // "선이 지난다" = 사이에 끼었다. 같은 면을 나란히 누르는(밀기) 경우는 선이 표면 위로 지나가 걸리지 않는다.
    const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GraspTrace), false);
    auto LineCrossesItem = [&](const FVector& A, const FVector& B)
    {
        FHitResult Hit;
        return Item.ItemMesh->LineTraceComponent(Hit, A, B, TraceParams);
    };

    // 손가락 오버랩은 물건마다 한 번 — 핀치·주먹 판정이 같은 결과를 나눠 쓴다.
    const FFingerTouches Touched = TouchingFingers(Item.ItemMesh, ContactGrabMargin);
    for (int32 Finger = 1; Finger < 5 && Touched[0]; ++Finger)
    {
        if (Touched[Finger] && LineCrossesItem(Touched[0]->GetComponentLocation(), Touched[Finger]->GetComponentLocation()))
        {
            OutPartner = Finger;
            return true;
        }
    }
    if (!IsPalmTouching(Item.ItemMesh, ContactGrabMargin)) return false;

    int32 WrappingFingers = 0;
    for (int32 Finger = 1; Finger < 5; ++Finger)
    {
        if (Touched[Finger] && LineCrossesItem(PalmCenter, Touched[Finger]->GetComponentLocation())) ++WrappingFingers;
    }
    if (WrappingFingers < 2) return false;

    OutPartner = PalmGripPartner;
    return true;
}

void UVRHandComponent::GetFingerObstacles(TArray<const UPrimitiveComponent*, TInlineAllocator<8>>& OutComponents, TArray<FNPCWorldCapsule>& OutBones) const
{
    static constexpr float ObstacleReach = 20.f;   // 손바닥 중심 → 손끝 + 여유(cm)
    FVector PalmCenter;
    UWorld* World = GetWorld();
    if (!GetPalmShapeCenter(PalmCenter) || !World) return;

    FCollisionObjectQueryParams ObjectTypes(ECC_PhysicsBody);
    ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, PalmCenter, FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(ObstacleReach),
                                    FCollisionQueryParams(SCENE_QUERY_STAT(FingerObstacles), false, GetOwner()));
    for (const FOverlapResult& Overlap : Overlaps)
    {
        const UPrimitiveComponent* Component = Overlap.GetComponent();
        if (!Component) continue;
        // 드랍 아이템은 몸체 메시만(트리거 등 부속 컴포넌트 제외), 정적 월드는 그대로.
        const ADroppedItemBase* Item = Cast<ADroppedItemBase>(Overlap.GetActor());
        if (Item ? Component == Item->ItemMesh : Component->GetCollisionObjectType() == ECC_WorldStatic) OutComponents.AddUnique(Component);
    }

    TArray<ACombatCharacter*> BoneOwners;
    CapsuleContact::GatherBoneCapsules(World, PalmCenter, ObstacleReach, OutBones, BoneOwners);
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
    const FBodyInstance* ItemBody = Item->ItemMesh->GetBodyInstance();
    const UPhysicalMaterial* Material = ItemBody ? ItemBody->GetSimplePhysicalMaterial() : nullptr;
    const float Friction = Material ? Material->Friction : 0.7f;

    if (bContact)
    {
        ComputeContactGrip(*Item->ItemMesh, Partner, Friction, OutForce, OutTorque, OutCenter);
        return;
    }

    // 컨트롤러 — 손가락 접촉이 없으니 손바닥 중심에서 주먹으로 감싸 쥔 것으로 본다.
    FVector PalmCenter;
    if (GetPalmShapeCenter(PalmCenter)) OutCenter = PalmCenter;
    const float NormalSum = ControllerGripForce * GripValue;
    OutForce = Friction * NormalSum;
    OutTorque = Friction * NormalSum * ControllerGripRadius;
}

void UVRHandComponent::ComputeContactGrip(const UPrimitiveComponent& ItemMesh, int32 Partner, float Friction, float& OutForce, float& OutTorque, FVector& OutCenter) const
{
    // 핸드트래킹 — 표면에 닿은 손가락마다 N = 쥐는 정도 × 강성, 접촉점 = 닿은 캡슐 중심.
    // 주먹(손바닥 짝)이면 손바닥이 손가락들의 반작용(엄지 뺀 ΣN)을 받는다.
    // 쥔 중심 = 접촉점들의 가운데(힘 가중 아님 — 닿은 순간엔 N 이 0 이라 정할 수 없다). 핀치면 두 손끝 사이라 그 점을 축으로 돈다.
    // 회전 팔 rᵢ = 중심 ↔ 접촉점, 최소 FingerPadRadius — 접촉 분포가 넓을수록(주먹) 덜 돈다. 쿨롱 마찰이라 면적은 힘 크기엔 안 들어간다.
    TArray<TPair<FVector, float>, TInlineAllocator<6>> ContactPoints;
    float FingerNormalSum = 0.f;
    const FFingerTouches Touched = TouchingFingers(&ItemMesh, ContactReleaseMargin);
    for (int32 Finger = 0; Finger < 5; ++Finger)
    {
        if (!Touched[Finger]) continue;
        const float PressForce = SqueezeStiffness * SmoothedSqueeze[Finger];
        ContactPoints.Add({ Touched[Finger]->GetComponentLocation(), PressForce });
        if (Finger > 0) FingerNormalSum += PressForce;
    }
    FVector PalmCenter;
    if (Partner == PalmGripPartner && GetPalmShapeCenter(PalmCenter) && IsPalmTouching(&ItemMesh, ContactReleaseMargin))
    {
        ContactPoints.Add({ PalmCenter, FingerNormalSum });
    }
    if (ContactPoints.IsEmpty()) return;

    OutCenter = FVector::ZeroVector;
    for (const TPair<FVector, float>& Point : ContactPoints) OutCenter += Point.Key / ContactPoints.Num();
    float NormalSum = 0.f, TorqueArmSum = 0.f;
    for (const TPair<FVector, float>& Point : ContactPoints)
    {
        NormalSum += Point.Value;
        TorqueArmSum += Point.Value * FMath::Max(FVector::Dist(Point.Key, OutCenter), FingerPadRadius);
    }
    OutForce = Friction * NormalSum;
    OutTorque = Friction * TorqueArmSum;
}

bool UVRHandComponent::CanHold(const ADroppedItemBase* Item) const
{
    if (!IsValid(Item) || !IsValid(Item->ItemMesh) || !GetWorld()) return false;
    const bool bFingerGrip = Contact.bHeld && Contact.Item.Get() == Item;
    float Force = 0.f, Torque = 0.f;
    FVector Center;
    ComputeGrip(Item, bFingerGrip, Contact.Partner, Force, Torque, Center);
    return Force + GetOtherHandGripForce(Item) >= Item->ItemMesh->CalculateMass() * FMath::Abs(GetWorld()->GetGravityZ());
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

    // 다른 손이 이미 기록한 물건이면 두 번째 손 — 제약만 걸고 인벤토리 기록은 먼저 쥔 손이 갖는다.
    const UVRHandComponent* OtherHand = GetOtherHand();
    Grab.bSecondary = OtherHand && OtherHand->Grab.Item.Get() == Item && Inventory->GetHeldItem(OtherHand->GetHandSlot()) == Item;

    // 제약 기준점 = 쥔 중심(핀치면 두 손끝 사이, 주먹·컨트롤러면 손바닥) — 아이템은 그 점을 축으로 무게 토크만큼 돈다.
    Grab.bContact = Contact.bHeld && Contact.Item.Get() == Item;
    Grab.Partner = Grab.bContact ? Contact.Partner : INDEX_NONE;
    float Force = 0.f, Torque = 0.f;
    FVector Center;
    ComputeGrip(Item, Grab.bContact, Grab.Partner, Force, Torque, Center);

    AttachGrabConstraint(*Item, Center, Force, Torque, bKeepCollision);

    // 튜닝용 — 쥘 때 물리 손이 실제 손에서 밀려난 거리, 질량, 쥔 점~무게중심 거리(토크 팔), 마찰 한계 대 무게.
    const float Mass = Item->ItemMesh->CalculateMass();
    UE_LOG(LogTemp, Log, TEXT("[VRHand] 쥠 상세: %s %s 손 어긋남 %.1fcm, 질량 %.2fkg, 무게중심 거리 %.1fcm, 힘 한계 %.0f(무게 %.0f), 토크 한계 %.0f, 충돌 %s"),
           *Item->ItemData.ItemTemplateID, Grab.bContact ? TEXT("접촉") : TEXT("컨트롤러"), FVector::Dist(GetComponentLocation(), Palm->GetComponentLocation()),
           Mass, FVector::Dist(Center, Item->ItemMesh->GetCenterOfMass()), Force, Mass * FMath::Abs(GetWorld()->GetGravityZ()), Torque,
           bKeepCollision ? TEXT("유지") : TEXT("끔"));

    if (!Grab.bSecondary) Inventory->HoldItem(Item, GetHandSlot());
    Grab.Item = Item;
    return true;
}

void UVRHandComponent::AttachGrabConstraint(ADroppedItemBase& Item, const FVector& Center, float Force, float Torque, bool bKeepCollision)
{
    // 드라이브 강성·감쇠 — 질량(회전은 쥔 점 기준 관성 = 무게중심 관성 최대축 + m·d²) × ω², 임계감쇠 2·질량·ω.
    // ponytail: 관성은 최대축 하나로 — 길쭉한 물건의 긴 축 회전은 실제보다 단단하다. 축별로 달라야 하면 관성 텐서를 제약 축으로 돌려 쓸 것.
    const float Mass = Item.ItemMesh->CalculateMass();
    const float Inertia = Item.ItemMesh->GetInertiaTensor().GetMax() + Mass * FVector::DistSquared(Center, Item.ItemMesh->GetCenterOfMass());
    Grab.LinStiffness = Mass * FMath::Square(GripLinearFrequency);
    Grab.LinDamping = 2.f * Mass * GripLinearFrequency;
    Grab.AngStiffness = Inertia * FMath::Square(GripAngularFrequency);
    Grab.AngDamping = 2.f * Inertia * GripAngularFrequency;
    Grab.LinTarget = FVector::ZeroVector;
    Grab.AngTarget = FQuat::Identity;

    // MaxForce 0 은 엔진에서 "무제한" — 쥐는 힘이 0 이어도 최소 1 로 둔다.
    GrabConstraint->SetLinearDriveParams(Grab.LinStiffness, Grab.LinDamping, FMath::Max(Force, 1.f));
    GrabConstraint->SetAngularDriveParams(Grab.AngStiffness, Grab.AngDamping, FMath::Max(Torque, 1.f));
    GrabConstraint->SetLinearPositionTarget(FVector::ZeroVector);
    GrabConstraint->SetAngularOrientationTarget(FRotator::ZeroRotator);
    const FTransform GripFrame(Palm->GetComponentQuat(), Center);
    GrabConstraint->SetWorldLocationAndRotation(GripFrame.GetLocation(), GripFrame.GetRotation());
    GrabConstraint->SetDisableCollision(!bKeepCollision);
    GrabConstraint->SetConstrainedComponents(Palm, NAME_None, Item.ItemMesh, NAME_None);
    Grab.FrameInPalm = GripFrame.GetRelativeTransform(Palm->GetComponentTransform());
    Grab.FrameInItem = GripFrame.GetRelativeTransform(Item.ItemMesh->GetComponentTransform());
}

void UVRHandComponent::ReleaseGrab()
{
    Grab = FGrabState();
    if (IsValid(GrabConstraint))
    {
        GrabConstraint->BreakConstraint();
    }
}

void UVRHandComponent::SyncGrab()
{
    ReleaseIfNoLongerHeld();

    const ADroppedItemBase* Item = Grab.Item.Get();
    if (!IsValid(Item) || !IsValid(Item->ItemMesh) || !IsPhysicsActive()) return;

    // 쥔 점이 손에서 너무 멀리 미끄러졌으면 놓쳤다(제약은 상한만 있어 스스로 끊기지 않는다).
    // 두 번째 손이 놓친 건 그 손만 놓으면 된다(물건은 기록 손이 들고 있다).
    const FTransform PalmInItem = UpdateGripLimits(*Item);
    if (PalmInItem.GetLocation().Size() > GripSlipReleaseDistance)
    {
        if (Grab.bSecondary) ReleaseGrab();
        else DropSlipped();
    }
}

void UVRHandComponent::ReleaseIfNoLongerHeld()
{
    const UInventoryComponent* Inventory = GetInventory();
    if (!Inventory || !Grab.Item.IsValid()) return;

    // 기록 손은 자기 슬롯이, 두 번째 손은 어느 슬롯이든 그 물건을 쥐고 있어야 한다. 아니면(수납·소모·기록 손이 놓음) 푼다.
    const ADroppedItemBase* HeldItem = Grab.Item.Get();
    const bool bStillInHand = Grab.bSecondary
        ? (Inventory->GetHeldItem(EEquipmentSlot::MainHand) == HeldItem || Inventory->GetHeldItem(EEquipmentSlot::OffHand) == HeldItem)
        : Inventory->GetHeldItem(GetHandSlot()) == HeldItem;
    if (!bStillInHand) ReleaseGrab();
}

FTransform UVRHandComponent::UpdateGripLimits(const ADroppedItemBase& Item)
{
    // 마찰 한계 갱신 — 쥐는 힘을 빼면 한계가 내려가 물건이 스르륵 빠진다.
    // ponytail: 엔진 상한은 축별(X·Y·Z) 클램프라 대각 방향은 최대 √3 배까지 버틴다. 원뿔이 필요하면 상한을 축 분해해 넣을 것.
    float Force = 0.f, Torque = 0.f;
    FVector Center;
    ComputeGrip(&Item, Grab.bContact, Grab.Partner, Force, Torque, Center);
    Grab.LastForce = Force;
    Force = FMath::Max(Force, 1.f);
    Torque = FMath::Max(Torque, 1.f);
    GrabConstraint->SetLinearDriveParams(Grab.LinStiffness, Grab.LinDamping, Force);
    GrabConstraint->SetAngularDriveParams(Grab.AngStiffness, Grab.AngDamping, Torque);

    // 재고착(정지마찰) — 드라이브가 한계에 걸려 밀린 만큼 목표를 현재 자세 쪽으로 옮겨 스프링 늘어남을 한계/강성으로 유지한다.
    // 안 하면 하중이 줄 때 스프링이 원래 자세로 튕겨 출렁인다(PIE 스파이크: 재고착 없으면 23°↔90° 진동).
    // 늘어남 한계는 엔진 실효 강성 기준 — Chaos 는 회전 드라이브 강성에 1.5 배(p.Chaos.JointConstraint.AngularDriveStiffnessScale)를 곱한다.
    // 넣은 강성으로 나누면 스프링이 늘 한계의 1.5 배로 포화돼 감쇠 몫이 없고, 평형각 둘레로 감쇠 없이 출렁인다(PIE: 횃불 끝 쥐기 10°↔49°).
    // 지금 자세 = 아이템 쪽 프레임 기준 손바닥 쪽 프레임 — 드라이브 목표 좌표. 반대(손바닥 기준 아이템)로 넣으면 목표가 거울 위치에 놓여
    // 드라이브가 운동 방향과 무관하게 한쪽으로만 상한만큼 밀어, 평형각 둘레로 감쇠 없이 출렁인다(PIE: 횃불 끝 쥐기 0°↔50°).
    const FTransform PalmFrame = Grab.FrameInPalm * Palm->GetComponentTransform();
    const FTransform ItemFrame = Grab.FrameInItem * Item.ItemMesh->GetComponentTransform();
    const FTransform PalmInItem = PalmFrame.GetRelativeTransform(ItemFrame);

    const FVector Stretch = Grab.LinTarget - PalmInItem.GetLocation();
    const float MaxStretch = Force / FMath::Max(Grab.LinStiffness, KINDA_SMALL_NUMBER);
    if (Stretch.Size() > MaxStretch)
    {
        Grab.LinTarget = PalmInItem.GetLocation() + Stretch.GetSafeNormal() * MaxStretch;
        GrabConstraint->SetLinearPositionTarget(Grab.LinTarget);
    }
    const float Twist = PalmInItem.GetRotation().AngularDistance(Grab.AngTarget);
    const float MaxTwist = Torque / FMath::Max(Grab.AngStiffness * AngularDriveStiffnessScale(), KINDA_SMALL_NUMBER);
    if (Twist > MaxTwist)
    {
        Grab.AngTarget = FQuat::Slerp(PalmInItem.GetRotation(), Grab.AngTarget, MaxTwist / Twist).GetNormalized();
        GrabConstraint->SetAngularOrientationTarget(Grab.AngTarget.Rotator());
    }
    return PalmInItem;
}

UVRHandComponent* UVRHandComponent::GetOtherHand() const
{
    TArray<UVRHandComponent*> Hands;
    if (GetOwner()) GetOwner()->GetComponents<UVRHandComponent>(Hands);
    for (UVRHandComponent* Candidate : Hands)
    {
        if (Candidate && Candidate != this) return Candidate;
    }
    return nullptr;
}

float UVRHandComponent::GetOtherHandGripForce(const ADroppedItemBase* Item) const
{
    const UVRHandComponent* Other = GetOtherHand();
    return Other && IsValid(Item) && Other->Grab.Item.Get() == Item ? Other->Grab.LastForce : 0.f;
}

bool UVRHandComponent::HandOverToOtherHand()
{
    UVRHandComponent* Other = GetOtherHand();
    UInventoryComponent* Inventory = GetInventory();
    const ADroppedItemBase* Item = Grab.Item.Get();
    if (!Other || !Inventory || Grab.bSecondary || !IsValid(Item) || !Other->Grab.bSecondary || Other->Grab.Item.Get() != Item) return false;

    // 기록만 넘긴다 — 물건은 그대로 다른 손 제약에 매달려 있다.
    Inventory->MoveHeldItem(GetHandSlot(), Other->GetHandSlot());
    Other->Grab.bSecondary = false;
    ReleaseGrab();
    UE_LOG(LogTemp, Log, TEXT("[VRHand] 양손 쥐기 — 기록 손 넘김: %s → %s"), IsLeft() ? TEXT("왼손") : TEXT("오른손"), Other->IsLeft() ? TEXT("왼손") : TEXT("오른손"));
    return true;
}

void UVRHandComponent::DropSlipped()
{
    if (HandOverToOtherHand()) return;   // 다른 손이 같이 쥐고 있으면 이 손만 놓친 것
    UInventoryComponent* Inventory = GetInventory();
    ReleaseGrab();
    // 던지기·건네기 분기 없이 그 자리에서 떨어진다(ReleaseHeldItem 이 물리를 되살린다).
    if (ADroppedItemBase* Dropped = Inventory ? Inventory->ReleaseHeldItem(GetHandSlot()) : nullptr)
    {
        UE_LOG(LogTemp, Log, TEXT("[VRHand] 놓침(미끄러짐): %s"), *Dropped->ItemData.ItemTemplateID);
    }
}
