#pragma once

#include "CoreMinimal.h"

class ACombatCharacter;
class UWorld;
struct FNPCWorldCapsule;

/** 접촉 형상 한 조각 — 형상 기준 로컬 선분 + 반지름. 손바닥·손가락·무기 등을 이것들의 묶음으로 근사한다. */
struct FContactSegment
{
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ZeroVector;
    float Radius = 0.f;
};

/**
 * 캡슐 접촉 물리 공용 헬퍼 — 형상을 NPC 뼈 캡슐 밖으로 밀어내기(관통 해소)와 접촉 깊이 비례 밀기.
 * 소유자 타입에 묶지 않아 플레이어 물리 손·NPC 손·무기 접촉이 같은 함수를 쓴다. 타격(½mv²)은 MeleeHitbox·KineticDamage.
 */
namespace CapsuleContact
{
    /** 관통 해소 반복 횟수 상한 — 밀린 곳에서 다른 캡슐에 박혔을 수 있어 몇 번 되풀이한다. */
    constexpr int32 MaxDepenetrationIters = 4;

    /** Location 에서 Reach(cm) 안으로 메시 경계가 걸친 전투 캐릭터들의 월드 뼈 캡슐. OutOwners 는 OutBones 와 같은 순서.
     *  뼈 캡슐 치수가 없는 NPC 는 빠진다(그 NPC 는 몸 캡슐이 막는다). */
    void GatherBoneCapsules(UWorld* World, const FVector& Location, float Reach,
                            TArray<FNPCWorldCapsule>& OutBones, TArray<ACombatCharacter*>& OutOwners);

    /**
     * 형상(로컬 선분들)을 Location·Rotation 에 놓았을 때 뼈 캡슐에 들어가면, 표면 밖까지 옮긴 위치를 돌려준다(회전은 그대로).
     * 밀어낼 방향은 ReferenceTM(형상이 지금 실제로 있는 곳) 쪽 — 목표가 뼈 축을 넘어가도 반대편 표면으로 튀지 않는다.
     * OutDeepestBone = 첫 반복(원래 위치)에서 가장 깊이 들어간 뼈 인덱스, 없으면 INDEX_NONE.
     */
    FVector ProjectOut(TArrayView<const FContactSegment> LocalSegments, const FVector& Location, const FQuat& Rotation,
                       const FTransform& ReferenceTM, TArrayView<const FNPCWorldCapsule> Bones, int32& OutDeepestBone);

    /** 표면(Surface)에서 관통점(Penetrator)까지 깊이에 비례한 수평 밀기 속도(cm/s) = (깊이 − MinDepth) × Gain, MaxSpeed 상한.
     *  MinDepth 미만이거나 수평 성분이 없으면 0. */
    FVector PushVelocity(const FVector& Surface, const FVector& Penetrator, float MinDepth, float Gain, float MaxSpeed);
}
