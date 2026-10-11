#include "Player/Components/VRMeleeComponent.h"

#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"

#include "Core/BP/CombatCharacter.h"
#include "Core/Physics/KineticDamage.h"
#include "Core/Physics/MeleeHitbox.h"
#include "Inventory/Components/InventoryComponent.h"

namespace
{
    // 막기 성공 화면 디버그 메시지 표시 시간(초).
    constexpr float BlockDebugMessageSeconds = 2.f;
}

UVRMeleeComponent::UVRMeleeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;   // 소유자가 손 갱신 뒤에 Update 를 부른다
}

void UVRMeleeComponent::Init(USceneComponent* InLeftAnchor, USceneComponent* InRightAnchor, UInventoryComponent* InInventory)
{
    LeftAnchor = InLeftAnchor;
    RightAnchor = InRightAnchor;
    Inventory = InInventory;

    // 앵커가 없으면 손 속도가 안 잡혀 타격·던지기가 전부 죽는다 — 조용히 넘어가면 원인 추적이 어렵다.
    if (!LeftAnchor || !RightAnchor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMelee] Init — 손 앵커 누락(왼 %d, 오른 %d). 근접 타격 비활성."),
            LeftAnchor != nullptr, RightAnchor != nullptr);
    }
    if (!Inventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMelee] Init — 인벤토리 누락. 무기 판정·막기 비활성(맨손만)."));
    }
}

FVector UVRMeleeComponent::SmoothHandVelocity(const FVector& PrevVel, const FVector& RawVel, float Smoothing)
{
    // 텔레포트·트래킹 튐은 ½mv² 데미지 폭주·물리 폭발로 이어진다. 속도(cm/s) 기준이라 프레임레이트 독립.
    const FVector Sample = RawVel.Size() > MaxPlausibleHandSpeed ? FVector::ZeroVector : RawVel;
    return FMath::Lerp(PrevVel, Sample, Smoothing);
}

void UVRMeleeComponent::Update(float DeltaTime)
{
    // 실제 손(트래킹 앵커) 속도 추적. ½mv² 의 v, 던지기 속도가 같이 쓴다.
    // 앵커는 kinematic 이라 GetVelocity()=0 → 위치 델타/dt 수동 산출. EMA 로 트래킹 스파이크 평탄화.
    // 컨트롤러 위치를 쓰면 핸드트래킹 중엔 내려놓은 컨트롤러라 속도가 0 — 던지면 떨어지고 타격이 안 들어간다.
    if (DeltaTime <= KINDA_SMALL_NUMBER || !IsValid(LeftAnchor) || !IsValid(RightAnchor)) return;

    const FVector Cur[2] = { LeftAnchor->GetComponentLocation(), RightAnchor->GetComponentLocation() };
    if (bHandVelInit)
    {
        for (int32 HandIdx = 0; HandIdx < 2; ++HandIdx)
        {
            HandVel[HandIdx] = SmoothHandVelocity(HandVel[HandIdx], (Cur[HandIdx] - PrevHandLoc[HandIdx]) / DeltaTime, HandVelSmoothing);
        }
        // 근접 타격 — 실제 손 위치에서 능동 오버랩(본 부착 패시브 overlap 회피). 오른손 먼저.
        TryMeleeHits(Cur[1], HandVel[1], /*bRightHand=*/true);
        TryMeleeHits(Cur[0], HandVel[0], /*bRightHand=*/false);
    }
    PrevHandLoc[0] = Cur[0];
    PrevHandLoc[1] = Cur[1];
    bHandVelInit = true;
}

void UVRMeleeComponent::ResetHandVelocity()
{
    // 근거리 리스폰은 MaxPlausibleHandSpeed 글리치 가드에도 걸리지 않아 허위 타격이 나간다.
    bHandVelInit = false;
    HandVel[0] = HandVel[1] = FVector::ZeroVector;
}

FMeleeHitbox UVRMeleeComponent::BuildHitbox(const FVector& HandLoc, bool bRightHand) const
{
    // 무기를 쥐고 있으면 판정 형상을 그 무기로 바꾼다. 손 주변 구만 보면 검을 휘둘러도
    // 칼날이 닿는 거리에서는 아무 일도 일어나지 않아 맨손과 사거리가 같아진다.
    FMeleeHitbox Box = MeleeHitbox::Sphere(HandLoc, MeleeSphereRadius, WeaponMass);
    if (Inventory) Inventory->GetHeldHitbox(HandSlotOf(bRightHand), Box);
    return Box;
}

void UVRMeleeComponent::TryMeleeHits(const FVector& HandLoc, const FVector& Vel, bool bRightHand)
{
    // 1단 임계 — MeleeStrikeSpeed 이상 스윙만 때리기(TakeDamage → SmartNPC 공격 인지, EnemyCharacter 반격) + 넉백.
    // 그 미만 접촉은 여기서 아무것도 안 한다 — 느린 손으로 미는 건 물리 손(UVRHandComponent::PushTouchedNPC)이 맡는다.
    // 대상은 ACombatCharacter(SmartNPC·EnemyCharacter) — 적 클래스도 같은 스윙·투사체 규약으로 맞는다.
    const float SpeedMs = KineticDamage::ToMetersPerSec(Vel);
    if (SpeedMs < MeleeStrikeSpeed) return;

    UWorld* World = GetWorld();
    if (!World) return;

    const FMeleeHitbox Box = BuildHitbox(HandLoc, bRightHand);
    TArray<ACombatCharacter*> Targets;
    MeleeHitbox::CollectTargets(World, Box, GetOwner(), Targets);

    const APawn* OwnerPawn = Cast<APawn>(GetOwner());
    AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
    const FVector HitDir    = Vel.GetSafeNormal();
    const float   Damage    = KineticDamage::Compute(Box.Mass, SpeedMs, KineticDamageScale, MaxKineticDamage);
    const float   PushSpeed = FMath::Min(SpeedMs * KnockbackScale, MaxKnockbackSpeed);
    const float   Now       = World->GetTimeSeconds();
    for (ACombatCharacter* NPC : Targets)
    {
        // 같은 NPC 재타격 쿨다운 — 매 틱 쿼리라 쿨다운 없으면 연속 타격 폭주. NPC 자신이 시각 보유.
        if (Now - NPC->LastMeleeHitTime < MeleeHitCooldown) continue;
        NPC->LastMeleeHitTime = Now;

        // 손 위치 기준 부위 인지 FPointDamageEvent — BoneName(부위 배율)·ShotDirection(래그돌 임펄스).
        KineticDamage::ApplyToNPC(NPC, Damage, HandLoc, HitDir, InstigatorController, GetOwner());

        // 넉백 — 죽었으면 HandleDeath 의 래그돌 임펄스가 처리.
        if (!NPC->bIsDead && KnockbackScale > 0.f)
        {
            NPC->LaunchCharacter(HitDir * PushSpeed, /*bXYOverride=*/true, /*bZOverride=*/false);
        }
    }
}

float UVRMeleeComponent::ApplyBlock(float Damage, const FVector& AttackerLoc, const FVector& DefenderLoc,
                                    const FVector& LeftHandLoc, const FVector& RightHandLoc) const
{
    // 아이템을 쥔 손이 공격자 쪽에 있으면 막은 것 — BlockDamageScale 만 남는다.
    if (Damage <= 0.f || !Inventory) return Damage;

    for (const bool bRight : { true, false })
    {
        if (!Inventory->GetHeldItem(HandSlotOf(bRight))) continue;
        if (!MeleeHitbox::IsFacing(DefenderLoc, bRight ? RightHandLoc : LeftHandLoc, AttackerLoc, BlockDotThreshold)) continue;

        const float Result = Damage * BlockDamageScale;
        LogBlock(bRight, Result, Damage);
        return Result;
    }
    return Damage;
}

void UVRMeleeComponent::LogBlock(bool bRight, float Result, float Damage) const
{
    const FString Msg = FString::Printf(TEXT("[VRMelee] Blocked (%s손) 데미지 %.1f (방어력 차감 후 %.1f)"),
        bRight ? TEXT("오른") : TEXT("왼"), Result, Damage);
    UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, BlockDebugMessageSeconds, FColor::Yellow, Msg);
}
