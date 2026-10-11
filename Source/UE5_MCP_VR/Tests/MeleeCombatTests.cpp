#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"

#include "Core/Physics/CapsuleContact.h"
#include "Core/Physics/KineticDamage.h"
#include "Core/Physics/MeleeHitbox.h"
#include "Core/Physics/NPCBoneCapsuleSet.h"
#include "Inventory/BP/ItemDataAsset.h"
#include "Player/Components/VRMeleeComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 근접 전투의 순수 계산(월드·액터 없이 돌아가는 부분) 회귀 검사. sol_pi verify 의 "Project" 필터에 잡힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeleeCombatMathTest, "Project.Combat.MeleeMath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMeleeCombatMathTest::RunTest(const FString& Parameters)
{
    // 손 → 슬롯
    TestTrue(TEXT("오른손 = MainHand"), HandSlotOf(true) == EEquipmentSlot::MainHand);
    TestTrue(TEXT("왼손 = OffHand"), HandSlotOf(false) == EEquipmentSlot::OffHand);

    // 속도 단위·½mv²
    TestEqual(TEXT("cm/s → m/s"), KineticDamage::ToMetersPerSec(FVector(300.f, 400.f, 0.f)), 5.f);
    TestEqual(TEXT("½·2kg·(10m/s)² = 100"), KineticDamage::Compute(2.f, 10.f, 1.f, 1000.f), 100.f);
    TestEqual(TEXT("상한 클램프"), KineticDamage::Compute(2.f, 10.f, 1.f, 50.f), 50.f);

    // 손 속도 EMA·트래킹 튐 차단
    TestEqual(TEXT("EMA 절반 섞임"),
        UVRMeleeComponent::SmoothHandVelocity(FVector::ZeroVector, FVector(100.f, 0.f, 0.f), 0.5f).X, 50.0);
    const FVector Glitch(UVRMeleeComponent::MaxPlausibleHandSpeed + 1.f, 0.f, 0.f);
    TestEqual(TEXT("튐은 0 으로 섞임"),
        UVRMeleeComponent::SmoothHandVelocity(FVector(100.f, 0.f, 0.f), Glitch, 0.5f).X, 50.0);

    // 맨손 판정 구
    const FMeleeHitbox Sphere = MeleeHitbox::Sphere(FVector(1.f, 2.f, 3.f), 12.f, 2.f);
    TestTrue(TEXT("맨손은 구"), Sphere.Shape.IsSphere());
    TestEqual(TEXT("구 반경"), Sphere.Shape.GetSphereRadius(), 12.f);
    TestEqual(TEXT("기본 질량"), Sphere.Mass, 2.f);

    // 막기 방향 — 수평 투영이라 높이 든 손도 공격자 쪽이면 막는다
    const FVector Defender = FVector::ZeroVector;
    const FVector Attacker(100.f, 0.f, 0.f);
    TestTrue(TEXT("앞으로 높이 든 손"), MeleeHitbox::IsFacing(Defender, FVector(30.f, 0.f, 80.f), Attacker, 0.6f));
    TestFalse(TEXT("등 뒤 손"), MeleeHitbox::IsFacing(Defender, FVector(-30.f, 0.f, 0.f), Attacker, 0.6f));
    TestFalse(TEXT("옆 손"), MeleeHitbox::IsFacing(Defender, FVector(0.f, 30.f, 0.f), Attacker, 0.6f));

    return true;
}

// 캡슐 접촉 물리(관통 해소·깊이 비례 밀기) 회귀 검사 — 월드 없이 순수 계산만.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapsuleContactTest, "Project.Physics.CapsuleContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCapsuleContactTest::RunTest(const FString& Parameters)
{
    // 세로 뼈 캡슐(반지름 10) 하나와 점 형상(반지름 2) — 표면 밖 = 축에서 12cm.
    const FNPCWorldCapsule Bone{ FVector(0.f, 0.f, -50.f), FVector(0.f, 0.f, 50.f), 10.f, NAME_None };
    const FContactSegment Point{ FVector::ZeroVector, FVector::ZeroVector, 2.f };
    const FTransform Reference(FVector(20.f, 0.f, 0.f));   // 형상이 실제로 있는 곳 = +X 쪽 바깥
    int32 Deepest = INDEX_NONE;

    const FVector Inside = CapsuleContact::ProjectOut(MakeArrayView(&Point, 1), FVector(5.f, 0.f, 0.f), FQuat::Identity,
                                                      Reference, MakeArrayView(&Bone, 1), Deepest);
    TestEqual(TEXT("관통 → 표면 밖(12cm)"), Inside.X, 12.0);
    TestEqual(TEXT("가장 깊은 뼈 인덱스"), Deepest, 0);

    // 축을 넘어 반대편(-X)에 있어도 기준(+X) 쪽 표면으로 — 몸 뒤로 미끄러지지 않는다.
    const FVector Crossed = CapsuleContact::ProjectOut(MakeArrayView(&Point, 1), FVector(-5.f, 0.f, 0.f), FQuat::Identity,
                                                       Reference, MakeArrayView(&Bone, 1), Deepest);
    TestEqual(TEXT("축 넘은 관통 → 들어온 쪽 표면"), Crossed.X, 12.0);

    const FVector Outside = CapsuleContact::ProjectOut(MakeArrayView(&Point, 1), FVector(30.f, 0.f, 0.f), FQuat::Identity,
                                                       Reference, MakeArrayView(&Bone, 1), Deepest);
    TestEqual(TEXT("안 겹치면 그대로"), Outside.X, 30.0);
    TestEqual(TEXT("안 겹치면 INDEX_NONE"), Deepest, static_cast<int32>(INDEX_NONE));

    // 깊이 비례 수평 밀기: (깊이 10 − 최소 3) × 8 = 56cm/s, 상한 120.
    const FVector Push = CapsuleContact::PushVelocity(FVector::ZeroVector, FVector(10.f, 0.f, 0.f), 3.f, 8.f, 120.f);
    TestEqual(TEXT("밀기 속도"), Push.X, 56.0);
    TestTrue(TEXT("최소 깊이 미만은 0"), CapsuleContact::PushVelocity(FVector::ZeroVector, FVector(2.f, 0.f, 0.f), 3.f, 8.f, 120.f).IsZero());
    TestTrue(TEXT("수직 관통은 0(수평만 민다)"), CapsuleContact::PushVelocity(FVector::ZeroVector, FVector(0.f, 0.f, 10.f), 3.f, 8.f, 120.f).IsZero());
    TestEqual(TEXT("상한 클램프"), CapsuleContact::PushVelocity(FVector::ZeroVector, FVector(100.f, 0.f, 0.f), 3.f, 8.f, 120.f).X, 120.0);

    // 형상 → 선분: 상자(절반 4×2×1, X+10)는 긴 축 X 를 따라 Y=±1 에 반지름 1 선분 두 줄(길이 절반 3), 캡슐(반지름 1·절반 높이 3)은 축 ±2.
    UBoxComponent* Box = NewObject<UBoxComponent>(GetTransientPackage());
    Box->SetBoxExtent(FVector(4.f, 2.f, 1.f), false);
    Box->SetRelativeLocation(FVector(10.f, 0.f, 0.f));
    UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(GetTransientPackage());
    Capsule->SetCapsuleSize(1.f, 3.f, false);
    const TObjectPtr<UBoxComponent> Boxes[] = { Box };
    const TObjectPtr<UCapsuleComponent> Capsules[] = { Capsule };
    TArray<FContactSegment> Segments;
    CapsuleContact::AppendSegments(Boxes, Capsules, Segments);
    if (TestEqual(TEXT("선분 수 = 상자 2 + 캡슐 1"), Segments.Num(), 3))
    {
        TestEqual(TEXT("상자 선분 시작"), Segments[0].Start, FVector(7.f, 1.f, 0.f));
        TestEqual(TEXT("상자 선분 끝"), Segments[0].End, FVector(13.f, 1.f, 0.f));
        TestEqual(TEXT("상자 둘째 줄"), Segments[1].Start, FVector(7.f, -1.f, 0.f));
        TestEqual(TEXT("상자 반지름 = 두께 절반"), Segments[0].Radius, 1.f);
        TestEqual(TEXT("캡슐 축"), Segments[2].End, FVector(0.f, 0.f, 2.f));
        TestEqual(TEXT("캡슐 반지름"), Segments[2].Radius, 1.f);
    }

    return true;
}

#endif
