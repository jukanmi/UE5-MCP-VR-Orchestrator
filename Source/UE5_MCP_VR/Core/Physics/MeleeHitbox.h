#pragma once

#include "CoreMinimal.h"

class ACombatCharacter;
class AActor;
class UStaticMeshComponent;
class UWorld;

/** 근접 타격 판정 형상 — 위치·회전·충돌 모양·충격 질량. 폰/NPC 어느 쪽에서든 같은 방식으로 만든다. */
struct FMeleeHitbox
{
    FVector Location = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    FCollisionShape Shape;
    float Mass = 0.f;   // kg — ½mv² 의 m
};

/**
 * 근접 타격 판정 공용 헬퍼 — 손에 쥔 무기/맨손의 판정 형상을 만들고 겹친 전투 캐릭터를 모은다.
 * 특정 액터(폰)에 묶이지 않아 NPC 가 무기를 들 때도 그대로 쓴다. 데미지 환산은 KineticDamage.
 */
namespace MeleeHitbox
{
    /** 질량 오버라이드 하한(kg) — 0 질량이면 ½mv² 가 0 이 되어 타격이 사라진다. */
    constexpr float MinMassKg = 0.01f;

    /** 맨손 판정 — 손 주변 구. 무기를 쥐면 FromMesh 로 덮어쓴다. */
    FMeleeHitbox Sphere(const FVector& HandLoc, float Radius, float DefaultMass);

    /**
     * 쥔 물건 메시의 회전된 상자로 Out 의 위치·회전·모양을 덮어쓴다(메시 없으면 false, Out 불변).
     * 질량은 메시에 질량 오버라이드가 있을 때만 덮어쓴다 — 없으면 Out.Mass(기본 질량)를 그대로 둔다.
     */
    bool FromMesh(const UStaticMeshComponent* Mesh, FMeleeHitbox& Out);

    /** 판정 형상에 겹친 전투 캐릭터(서 있는 캡슐 + 넉다운 래그돌). Ignore 는 제외(자기 자신). */
    void CollectTargets(UWorld* World, const FMeleeHitbox& Box, const AActor* Ignore, TArray<ACombatCharacter*>& Out);

    /**
     * 손이 공격자 쪽에 있는가(막기 판정) — 방어자→손, 방어자→공격자 수평 방향 내적 ≥ DotThreshold.
     * 수평 투영 이유: 손은 캡슐 중심보다 늘 위에 있어 3D 내적으론 높이 든 손이 방어로 안 잡힌다.
     */
    bool IsFacing(const FVector& DefenderLoc, const FVector& HandLoc, const FVector& AttackerLoc, float DotThreshold);
}
