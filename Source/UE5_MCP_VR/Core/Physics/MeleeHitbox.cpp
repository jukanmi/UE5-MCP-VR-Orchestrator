#include "Core/Physics/MeleeHitbox.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"

#include "Core/BP/CombatCharacter.h"

FMeleeHitbox MeleeHitbox::Sphere(const FVector& HandLoc, float Radius, float DefaultMass)
{
    FMeleeHitbox Box;
    Box.Location = HandLoc;
    Box.Shape = FCollisionShape::MakeSphere(Radius);
    Box.Mass = DefaultMass;
    return Box;
}

bool MeleeHitbox::FromMesh(const UStaticMeshComponent* Mesh, FMeleeHitbox& Out)
{
    const UStaticMesh* StaticMesh = Mesh ? Mesh->GetStaticMesh() : nullptr;
    if (!StaticMesh) return false;

    // 회전은 컴포넌트에서 받고 크기는 로컬 바운즈에서 받는다. 월드 바운즈의 Extent 는
    // 축 정렬이라 기울어진 검일수록 실제보다 큰 상자가 된다.
    const FBoxSphereBounds& Bounds = StaticMesh->GetBounds();
    Out.Location = Mesh->GetComponentTransform().TransformPosition(Bounds.Origin);
    Out.Rotation = Mesh->GetComponentQuat();
    Out.Shape = FCollisionShape::MakeBox(Bounds.BoxExtent * Mesh->GetComponentScale());

    // 질량은 DroppedItemBase::BeginPlay 가 테이블 Weight 로 박아 둔 오버라이드를 쓴다.
    // GetMass() 는 쓰면 안 된다: 쥔 동안 물리가 꺼져 있어 매 프레임 경고를 찍고 0 을 돌려주므로
    // 근접 피해가 0.01kg 기준으로 뭉개진다.
    const FBodyInstance* Body = Mesh->GetBodyInstance();
    if (Body && Body->bOverrideMass) Out.Mass = FMath::Max(Body->GetMassOverride(), MinMassKg);
    return true;
}

void MeleeHitbox::CollectTargets(UWorld* World, const FMeleeHitbox& Box, const AActor* Ignore, TArray<ACombatCharacter*>& Out)
{
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MeleeHitbox] CollectTargets — World 없음, 판정 생략"));
        return;
    }

    // 능동 오버랩 — 본 부착 패시브 overlap 의 불안정 회피.
    // Pawn(서 있는 NPC 캡슐) + PhysicsBody(넉다운 래그돌 메시) 둘 다 — 쓰러진 NPC 저글 타격 가능(의도된 동작).
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeHit), /*bTraceComplex=*/false, Ignore);
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_Pawn);
    ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);

    TArray<FOverlapResult> Overlaps;
    if (!World->OverlapMultiByObjectType(Overlaps, Box.Location, Box.Rotation, ObjParams, Box.Shape, Params)) return;

    for (const FOverlapResult& Overlap : Overlaps)
    {
        if (ACombatCharacter* NPC = Cast<ACombatCharacter>(Overlap.GetActor())) Out.AddUnique(NPC);
    }
}

bool MeleeHitbox::IsFacing(const FVector& DefenderLoc, const FVector& HandLoc, const FVector& AttackerLoc, float DotThreshold)
{
    const FVector HandDir = (HandLoc - DefenderLoc).GetSafeNormal2D();
    const FVector AttackDir = (AttackerLoc - DefenderLoc).GetSafeNormal2D();
    return FVector::DotProduct(HandDir, AttackDir) >= DotThreshold;
}
