#include "Core/BP/VRMeleeComponent.h"

#include "Core/BP/CombatCharacter.h"
#include "Core/BP/VRHandComponent.h"
#include "Core/BP/VRPawn.h"
#include "Core/Physics/KineticDamage.h"
#include "Core/Types/GameStateData.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Inventory/BP/DroppedItemBase.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/BP/SmartNPC.h"
#include "Utils/DiceSystem.h"

UVRMeleeComponent::UVRMeleeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;   // 폰이 손 갱신 뒤에 Update 를 부른다
}

AVRPawn* UVRMeleeComponent::GetPawn() const
{
    return Cast<AVRPawn>(GetOwner());
}

void UVRMeleeComponent::Update(float DeltaTime)
{
    // 실제 손(트래킹 앵커) 속도 추적. ½mv² 의 v, 던지기 속도, 패링 판정이 같이 쓴다.
    // 앵커는 kinematic 이라 GetVelocity()=0 → 위치 델타/dt 수동 산출. EMA 로 트래킹 스파이크 평탄화.
    // 컨트롤러 위치를 쓰면 핸드트래킹 중엔 내려놓은 컨트롤러라 속도가 0 — 던지면 떨어지고 타격이 안 들어간다.
    const AVRPawn* Pawn = GetPawn();
    if (!Pawn || DeltaTime <= KINDA_SMALL_NUMBER || !IsValid(Pawn->HandLeft) || !IsValid(Pawn->HandRight)) return;

    const FVector Cur[2] = { Pawn->HandLeft->GetComponentLocation(), Pawn->HandRight->GetComponentLocation() };
    if (bHandVelInit)
    {
        for (int32 h = 0; h < 2; ++h)
        {
            // 텔레포트·트래킹 튐 방지 — 속도 >9000cm/s(90m/s, 인간 스윙 ~10m/s 불가)는 글리치로 보고
            // 0 처리. ½mv² 데미지 폭주·물리 폭발 차단. 속도(cm/s) 기준이라 프레임레이트 독립(저FPS 정상스윙 오판 X).
            const FVector Vel = (Cur[h] - PrevHandLoc[h]) / DeltaTime;
            HandVel[h] = FMath::Lerp(HandVel[h], Vel.Size() > 9000.f ? FVector::ZeroVector : Vel, HandVelSmoothing);
        }
        // 근접 타격 — 실제 손 위치에서 능동 스피어 오버랩(본 부착 패시브 overlap 회피). 오른손 먼저.
        TryMeleeHits(Cur[1], HandVel[1], /*bRightHand=*/true);
        TryMeleeHits(Cur[0], HandVel[0], /*bRightHand=*/false);
    }
    PrevHandLoc[0] = Cur[0];
    PrevHandLoc[1] = Cur[1];
    bHandVelInit = true;
}

void UVRMeleeComponent::ResetHandVelocity()
{
    // 근거리 리스폰은 9000cm/s 글리치 가드에도 걸리지 않아 허위 타격이 나간다.
    bHandVelInit = false;
    HandVel[0] = HandVel[1] = FVector::ZeroVector;
}

void UVRMeleeComponent::TryMeleeHits(const FVector& HandLoc, const FVector& Vel, bool bRightHand)
{
    // 2단 임계 — bPush(밀치기) 이상이면 밀고, bStrike(데미지) 이상이면 공격(TakeDamage→피격자 인지).
    // 대상은 ACombatCharacter(SmartNPC·EnemyCharacter) — 적 클래스도 같은 스윙·투사체 규약으로 맞는다.
    // 가벼운 밀침(bPush~bStrike 사이)은 데미지 없음 = LLM 이 공격으로 안 봄.
    const float SpeedMs = Vel.Size() / 100.f;          // cm/s → m/s
    const bool  bPush   = SpeedMs >= MinImpactSpeed;
    const bool  bStrike = SpeedMs >= MeleeStrikeSpeed;
    if (!bPush) return;

    AVRPawn* Pawn = GetPawn();
    UWorld* World = GetWorld();
    if (!Pawn || !World) return;

    // 무기를 쥐고 있으면 판정 형상을 그 무기로 바꾼다. 손 주변 구만 보면 검을 휘둘러도
    // 칼날이 닿는 거리에서는 아무 일도 일어나지 않아 맨손과 사거리가 같아진다.
    const EEquipmentSlot HandSlot = bRightHand ? EEquipmentSlot::MainHand : EEquipmentSlot::OffHand;
    const ADroppedItemBase* Held = Pawn->Inventory ? Pawn->Inventory->GetHeldItem(HandSlot) : nullptr;
    const UStaticMeshComponent* HeldMesh = (Held && Held->ItemMesh) ? Held->ItemMesh : nullptr;

    FVector QueryLoc = HandLoc;
    FQuat QueryRot = FQuat::Identity;
    FCollisionShape QueryShape = FCollisionShape::MakeSphere(MeleeSphereRadius);
    float ImpactMass = WeaponMass;

    if (HeldMesh && HeldMesh->GetStaticMesh())
    {
        // 회전은 컴포넌트에서 받고 크기는 로컬 바운즈에서 받는다. 월드 바운즈의 Extent 는
        // 축 정렬이라 기울어진 검일수록 실제보다 큰 상자가 된다.
        const FVector LocalExtent = HeldMesh->GetStaticMesh()->GetBounds().BoxExtent * HeldMesh->GetComponentScale();

        QueryLoc = HeldMesh->GetComponentTransform().TransformPosition(HeldMesh->GetStaticMesh()->GetBounds().Origin);
        QueryRot = HeldMesh->GetComponentQuat();
        QueryShape = FCollisionShape::MakeBox(LocalExtent);

        // 질량도 쥔 물건 것으로 — DroppedItemBase::BeginPlay 가 테이블 Weight 를 질량 오버라이드로 박아 뒀다.
        // GetMass() 는 쓰면 안 된다: 쥔 동안 물리가 꺼져 있어 매 프레임 경고를 찍고 0 을 돌려주므로
        // 근접 피해가 0.01kg 기준으로 뭉개진다. 오버라이드 없는 메시는 기본 WeaponMass.
        if (const FBodyInstance* Body = HeldMesh->GetBodyInstance(); Body && Body->bOverrideMass)
        {
            ImpactMass = FMath::Max(Body->GetMassOverride(), 0.01f);
        }
    }

    // 능동 오버랩(Pawn 채널) — 패시브 overlap 의 본부착 불안정 회피.
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeHit), /*bTraceComplex=*/false, Pawn);
    // Pawn(서 있는 NPC 캡슐) + PhysicsBody(넉다운 래그돌 메시) 둘 다 — 쓰러진 NPC 저글 타격 가능(의도된 동작).
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_Pawn);
    ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
    if (!World->OverlapMultiByObjectType(Overlaps, QueryLoc, QueryRot, ObjParams, QueryShape, Params)) return;

    const float Now = World->GetTimeSeconds();
    const float Damage = KineticDamage::Compute(ImpactMass, SpeedMs, KineticDamageScale, MaxKineticDamage);

    for (const FOverlapResult& O : Overlaps)
    {
        ACombatCharacter* NPC = Cast<ACombatCharacter>(O.GetActor());
        if (!NPC) continue;

        // 같은 NPC 재타격 쿨다운 — 매 틱 쿼리라 쿨다운 없으면 연속 타격 폭주. NPC 자신이 시각 보유.
        if (Now - NPC->LastMeleeHitTime < MeleeHitCooldown) continue;
        NPC->LastMeleeHitTime = Now;

        // 강타(bStrike) → 데미지. SmartNPC::TakeDamage 가 인지 이벤트(공격)를, EnemyCharacter 는 반격 타겟팅을 함.
        // 가벼운 밀침(bStrike 미만)은 TakeDamage 를 안 불러 NPC 가 공격으로 인지하지 않음.
        if (bStrike)
        {
            // RNG 패링(SPEC_realistic_combat §3.2) — LLM 인지가 있는 ASmartNPC 한정(필드 몹은 항상 피격).
            // 성공 확률 = Agility / ParryDifficulty(UDiceSystem::CheckReflex 공식).
            ASmartNPC* SmartTarget = Cast<ASmartNPC>(NPC);
            FDiceResult ParryRoll;
            const bool bParried = SmartTarget && SmartTarget->StateComponent
                && UDiceSystem::CheckReflex(SmartTarget->StateComponent->GetAttributes().BaseStats.Agility,
                                             SmartTarget->StateComponent->ParryDifficulty, ParryRoll);
            if (bParried)
            {
                // 데미지 무효 — 사운드 + LLM 인지용 이벤트만 큐잉(§2.3, emergency_report 로 이어짐).
                if (SmartTarget->ParrySound)
                {
                    UGameplayStatics::PlaySoundAtLocation(this, SmartTarget->ParrySound, SmartTarget->GetActorLocation());
                }
                const FPerceptionData ParryPerc(
                    ASmartNPC::PerceptionIdFor(Pawn), ESenseType::Parried,
                    Pawn->GetActorLocation(), SmartTarget->GetActorLocation(), 1.0f);
                SmartTarget->StateComponent->RequestEventCognition(ParryPerc);
            }
            else
            {
                // 손 위치 기준 부위 인지 FPointDamageEvent — BoneName(부위 배율)·ShotDirection(래그돌 임펄스).
                KineticDamage::ApplyToNPC(NPC, Damage, HandLoc, Vel.GetSafeNormal(), Pawn->GetController(), Pawn);
            }
        }

        // 밀치기 — 가벼운 접촉도 밀되 공격 인지는 없음. 죽었으면 HandleDeath 의 래그돌 임펄스가 처리.
        if (!NPC->bIsDead && KnockbackScale > 0.f)
        {
            const float PushSpeed = FMath::Min(SpeedMs * KnockbackScale, MaxKnockbackSpeed);
            NPC->LaunchCharacter(Vel.GetSafeNormal() * PushSpeed, /*bXYOverride=*/true, /*bZOverride=*/false);
        }
    }
}

float UVRMeleeComponent::ApplyBlock(float Damage, const AActor* DamageCauser) const
{
    // Block/Parry — 아이템을 쥔 손이 공격자 쪽(수평 내적 ≥ BlockDotThreshold)에 있으면 막은 것.
    // 그 손이 ParryHandSpeed 이상으로 움직이는 중이면 패링(데미지 0), 아니면 단순 방어(BlockDamageScale).
    // 수평 투영 이유: 손은 캡슐 중심보다 늘 위에 있어 3D 내적으론 높이 든 손이 방어로 안 잡힘.
    const AVRPawn* Pawn = GetPawn();
    if (Damage <= 0.f || !DamageCauser || !Pawn || !Pawn->Inventory) return Damage;

    const FVector PlayerLoc = Pawn->GetActorLocation();
    const FVector AttackDir = (DamageCauser->GetActorLocation() - PlayerLoc).GetSafeNormal2D();
    for (const bool bRight : { true, false })
    {
        if (!Pawn->Inventory->GetHeldItem(bRight ? EEquipmentSlot::MainHand : EEquipmentSlot::OffHand)) continue;
        const FVector HandDir = (Pawn->GetHandLocation(bRight) - PlayerLoc).GetSafeNormal2D();
        if (FVector::DotProduct(HandDir, AttackDir) < BlockDotThreshold) continue;

        const float HandSpeed = GetHandVelocity(!bRight).Size();
        const bool  bParry    = HandSpeed >= ParryHandSpeed;
        const float Result    = bParry ? 0.f : Damage * BlockDamageScale;
        const FString Msg = FString::Printf(TEXT("[VRPawn] %s (%s손 %.0f cm/s) 데미지 %.1f (방어력 차감 후 %.1f)"),
            bParry ? TEXT("Parried") : TEXT("Blocked"), bRight ? TEXT("오른") : TEXT("왼"), HandSpeed, Result, Damage);
        UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, bParry ? FColor::Cyan : FColor::Yellow, Msg);
        return Result;
    }
    return Damage;
}
