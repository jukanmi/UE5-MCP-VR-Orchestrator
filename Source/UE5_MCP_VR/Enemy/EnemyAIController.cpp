#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyCharacter.h"
#include "Core/Interfaces/Entity.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AISense_Sight.h"
#include "EngineUtils.h"

AEnemyAIController::AEnemyAIController()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.15f;  // FSM 판단 주기 — 매 프레임 필요 없음

    PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerceptionComponent"));
    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    if (PerceptionComp && SightConfig)
    {
        SightConfig->SightRadius = SightRadius;
        SightConfig->LoseSightRadius = LoseSightRadius;
        SightConfig->PeripheralVisionAngleDegrees = SightAngle;
        SightConfig->SetMaxAge(3.0f);
        // 팀 개념 없음(기본 Attitude=Neutral) → 중립 감지 필수.
        SightConfig->DetectionByAffiliation.bDetectEnemies = true;
        SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
        SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
        PerceptionComp->ConfigureSense(*SightConfig);
        PerceptionComp->SetDominantSense(SightConfig->GetSenseImplementation());
        PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &AEnemyAIController::OnTargetPerceptionUpdated);
    }
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    Home = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
    State = EEnemyState::Wander;
    NextWanderTime = 0.f;
    bHasFled = false;
    HoldSide = FMath::RandBool() ? 1.f : -1.f;

    // BP 에서 바꾼 시야값 반영(생성자 값은 CDO 기본).
    if (PerceptionComp && SightConfig)
    {
        SightConfig->SightRadius = SightRadius;
        SightConfig->LoseSightRadius = LoseSightRadius;
        SightConfig->PeripheralVisionAngleDegrees = SightAngle;
        PerceptionComp->ConfigureSense(*SightConfig);
    }
    SetRunning(false);
}

AEnemyCharacter* AEnemyAIController::GetEnemy() const
{
    return Cast<AEnemyCharacter>(GetPawn());
}

bool AEnemyAIController::IsValidTarget(const AActor* Actor) const
{
    // 폰만(아이템·트리거 제외), 살아 있고, 같은 적 종류·자기 자신이 아닐 것.
    return Actor && Actor != GetPawn() && Actor->IsA<APawn>() && !Actor->IsA<AEnemyCharacter>()
        && !ACombatCharacter::IsActorDead(Actor);
}

void AEnemyAIController::SetTarget(AActor* NewTarget, bool bAlertOthers)
{
    if (!IsValidTarget(NewTarget)) return;
    const APawn* Me = GetPawn();
    if (AActor* Cur = Target.Get(); Cur && Me && Cur != NewTarget)
    {
        // 이미 싸우는 중이면 더 가까운 쪽으로만 갈아탄다 — 원거리 저격에 매번 끌려다니지 않게.
        if (FVector::DistSquared(Me->GetActorLocation(), Cur->GetActorLocation())
            <= FVector::DistSquared(Me->GetActorLocation(), NewTarget->GetActorLocation()))
            return;
    }
    const bool bNew = Target.Get() != NewTarget;
    Target = NewTarget;
    // 이미 시야 안이면 visible 로 시작 — perception 은 변화 때만 이벤트를 주므로, 경보로 받은 타겟이
    // 진작 보이고 있었으면 "본 적 없음" 으로 남아 LoseTargetTime 뒤 포기하는 사고가 난다.
    bTargetVisible = PerceptionComp && PerceptionComp->HasActiveStimulus(*NewTarget, UAISense::GetSenseID<UAISense_Sight>());
    LastSeenTime = GetWorld()->GetTimeSeconds();  // LoseTargetTime 만큼 찾을 여유
    if (State != EEnemyState::Flee) State = EEnemyState::Chase;
    UE_LOG(LogTemp, Log, TEXT("[EnemyAI] %s 타겟 %s (%s, visible=%d)"), *GetNameSafe(Me), *GetNameSafe(NewTarget),
        bAlertOthers ? TEXT("직접") : TEXT("경보"), bTargetVisible);
    if (bNew && bAlertOthers) AlertNearby(NewTarget);
}

void AEnemyAIController::AlertNearby(AActor* T)
{
    const APawn* Me = GetPawn();
    if (!Me || AlertRadius <= 0.f) return;
    const float R2 = AlertRadius * AlertRadius;
    for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
    {
        AEnemyCharacter* Other = *It;
        if (Other == Me || Other->bIsDead) continue;
        if (FVector::DistSquared(Other->GetActorLocation(), Me->GetActorLocation()) > R2) continue;
        if (AEnemyAIController* AIC = Cast<AEnemyAIController>(Other->GetController()))
        {
            if (!AIC->Target.IsValid()) AIC->SetTarget(T, /*bAlertOthers=*/false);
        }
    }
}

bool AEnemyAIController::IsEngaging(const AActor* T) const
{
    return bEngaged && T && Target.Get() == T && GetPawn() != nullptr;
}

int32 AEnemyAIController::CountEngaging(const AActor* T) const
{
    int32 N = 0;
    for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
    {
        if (*It == GetPawn() || It->bIsDead) continue;
        if (const AEnemyAIController* AIC = Cast<AEnemyAIController>(It->GetController()))
            if (AIC->IsEngaging(T)) ++N;
    }
    return N;
}

void AEnemyAIController::ClearTarget()
{
    Target = nullptr;
    bTargetVisible = false;
    bEngaged = false;
}

FString AEnemyAIController::GetDebugState() const
{
    static const TCHAR* Names[] = { TEXT("Wander"), TEXT("Chase"), TEXT("Hold"), TEXT("Flee"), TEXT("Return") };
    const AActor* T = Target.Get();
    const APawn* Me = GetPawn();
    const float D = (T && Me) ? FVector::Dist(Me->GetActorLocation(), T->GetActorLocation()) : -1.f;
    return FString::Printf(TEXT("%s eng=%d vis=%d tgt=%s d=%.0f move=%d"), Names[(int32)State], bEngaged, bTargetVisible,
        *GetNameSafe(T), D, (int32)GetMoveStatus());
}

void AEnemyAIController::OnPawnDied()
{
    StopMovement();
    ClearTarget();
    SetActorTickEnabled(false);
}

void AEnemyAIController::SetRunning(bool bRun)
{
    AEnemyCharacter* E = GetEnemy();
    if (!E) return;
    if (UCharacterMovementComponent* CMC = E->GetCharacterMovement())
    {
        CMC->MaxWalkSpeed = bRun ? E->Attributes.Movement.RunSpeed : E->Attributes.Movement.WalkSpeed;
    }
}

void AEnemyAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (!IsValidTarget(Actor)) return;
    const float Now = GetWorld()->GetTimeSeconds();

    if (Stimulus.WasSuccessfullySensed())
    {
        // 시야 자동 어그로는 플레이어만. 아군 NPC 는 자기를 때렸을 때만(SetTarget) 상대한다.
        if (!Target.IsValid() && Actor->Implements<UPlayerBase>())
        {
            SetTarget(Actor);  // 무리 경보 포함
        }
        if (Target.Get() == Actor)
        {
            bTargetVisible = true;
            LastSeenTime = Now;
        }
    }
    else if (Target.Get() == Actor)
    {
        bTargetVisible = false;
        LastSeenTime = Now;
    }
}

void AEnemyAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    AEnemyCharacter* E = GetEnemy();
    if (!E || E->bIsDead) return;
    if (E->IsAttacking()) return;  // 스윙 중엔 이동·재판단 없음

    const float Now = GetWorld()->GetTimeSeconds();
    AActor* T = Target.Get();
    if (T && !IsValidTarget(T))
    {
        ClearTarget();
        T = nullptr;
    }

    if (T) TickWithTarget(E, T, Now);
    else   TickWithoutTarget(E, Now);
}

void AEnemyAIController::TickWithTarget(AEnemyCharacter* E, AActor* T, float Now)
{
    const FVector MyLoc = E->GetActorLocation();
    const float Dist = FVector::Dist(MyLoc, T->GetActorLocation());

    // 포기 조건 — 시야 상실 지속 또는 리시 초과 → 걸어서 홈으로.
    // 아직 타겟 쪽으로 이동 중(경보로 받아 달려가는 중·나무에 가림)이면 3배 유예 — 도착도 못 하고 포기하지 않게.
    // 코앞(홀드 링 2배 이내)이면 시야 판정 자체를 안 한다 — 포위 대기 중 앞 동료 캡슐이 LOS 를 가려 포기하던 실측.
    const float LoseT = GetMoveStatus() == EPathFollowingStatus::Moving ? LoseTargetTime * 3.f : LoseTargetTime;
    const bool bLost = !bTargetVisible && (Now - LastSeenTime) > LoseT && Dist > HoldDistance * 2.f;
    if (bLost || FVector::Dist2D(MyLoc, Home) > LeashRadius)
    {
        UE_LOG(LogTemp, Log, TEXT("[EnemyAI] %s 포기(%s) → 복귀"), *E->GetName(), bLost ? TEXT("시야 상실") : TEXT("리시"));
        ClearTarget();
        State = EEnemyState::Return;
        SetRunning(false);
        MoveToLocation(Home, 80.f);
        return;
    }

    // 도주 — 생애 1회, HP 임계 아래로 처음 떨어졌을 때. 끝나면 재교전.
    if (State == EEnemyState::Flee)
    {
        if (Now < FleeUntil) return;
        State = EEnemyState::Chase;
        LastSeenTime = Now;  // 도망치느라 시야를 잃었어도 돌아갈 시간은 준다
    }
    else if (!bHasFled && E->FleeHealthPct > 0.f && E->GetHealthPercent() < E->FleeHealthPct)
    {
        StartFlee(E, T, Now);
        return;
    }

    // 공격 슬롯 — 이미 MaxAttackers 명이 치고 있으면 링에서 대기(포위). 내가 슬롯을 쥐고 있으면 유지.
    // 판정 창은 링 반경까지 — 좁으면(사거리×1.5) 링(380)으로 나간 순간 창 밖이라 추적↔대기를 매 틱 오간다(실측).
    if (Dist <= FMath::Max(AttackRange * 1.5f, HoldDistance + 120.f) && !bEngaged && CountEngaging(T) >= MaxAttackers)
    {
        HoldAround(E, T, Now);
        return;
    }

    if (Dist <= AttackRange)
    {
        // 사거리 안 — 슬롯 획득, 정지, 타겟 바라보기, 쿨다운마다 공격.
        if (!bEngaged) UE_LOG(LogTemp, Log, TEXT("[EnemyAI] %s 교전(슬롯 %d/%d)"), *E->GetName(), CountEngaging(T) + 1, MaxAttackers);
        bEngaged = true;
        State = EEnemyState::Chase;
        StopMovement();
        FRotator Face = (T->GetActorLocation() - MyLoc).Rotation();
        Face.Pitch = 0.f;
        Face.Roll = 0.f;
        E->SetActorRotation(Face);
        if (Now >= NextAttackTime)
        {
            const float Busy = E->StartAttack(T);
            if (Busy > 0.f) NextAttackTime = Now + FMath::Max(Busy, E->AttackCooldown);
        }
        return;
    }

    // 추적 — 이미 같은 타겟으로 이동 중이면 요청 반복 안 함(MoveToActor 가 움직이는 목표를 따라 재경로).
    const UPathFollowingComponent* PFC = GetPathFollowingComponent();
    const bool bMovingToTarget = PFC && PFC->GetStatus() == EPathFollowingStatus::Moving && PFC->GetMoveGoal() == T;
    if (!bMovingToTarget || State == EEnemyState::Hold)  // Hold 에서 슬롯이 나면 링 이동을 끊고 다시 붙는다
    {
        SetRunning(true);
        // bStopOnOverlap=false — true 면 양쪽 캡슐 반경(34+34)을 더해 ~187cm 에서 "도착" 처리돼
        // AttackRange(170) 밖에서 멈춘 채 공격도 이동도 안 하는 교착(실측).
        if (MoveToActor(T, AttackRange * 0.5f, /*bStopOnOverlap=*/false) == EPathFollowingRequestResult::Failed)
        {
            // 타겟이 내비 밖(바위·상자 위)이면 경로 자체가 실패해 제자리에 선다 — 타겟 근처 내비 지점으로 대신 간다.
            FNavLocation Near;
            UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
            if (Nav && Nav->ProjectPointToNavigation(T->GetActorLocation(), Near, FVector(250.f, 250.f, 400.f)))
                MoveToLocation(Near.Location, AttackRange * 0.5f, /*bStopOnOverlap=*/false);
        }
    }
    State = EEnemyState::Chase;
}

void AEnemyAIController::StartFlee(AEnemyCharacter* E, AActor* T, float Now)
{
    bHasFled = true;
    bEngaged = false;
    State = EEnemyState::Flee;
    FleeUntil = Now + E->FleeDuration;
    SetRunning(true);
    UE_LOG(LogTemp, Log, TEXT("[EnemyAI] %s 도주 (HP %.0f%%)"), *E->GetName(), E->GetHealthPercent() * 100.f);
    // 타겟 반대 방향 12m 근처 도달 가능 지점. 없으면 홈으로.
    const FVector Away = (E->GetActorLocation() - T->GetActorLocation()).GetSafeNormal2D();
    const FVector Want = E->GetActorLocation() + Away * 1200.f;
    FNavLocation Dest;
    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
    if (Nav && Nav->GetRandomReachablePointInRadius(Want, 400.f, Dest)) MoveToLocation(Dest.Location, 60.f);
    else MoveToLocation(Home, 80.f);
}

void AEnemyAIController::HoldAround(AEnemyCharacter* E, AActor* T, float Now)
{
    if (State != EEnemyState::Hold) UE_LOG(LogTemp, Log, TEXT("[EnemyAI] %s 포위 대기(슬롯 만석)"), *E->GetName());
    State = EEnemyState::Hold;
    bEngaged = false;
    // 타겟 바라보기 — 링을 돌면서도 정면 유지(플레이어가 등 뒤 노출 없이 포위감).
    FRotator Face = (T->GetActorLocation() - E->GetActorLocation()).Rotation();
    Face.Pitch = 0.f;
    Face.Roll = 0.f;
    E->SetActorRotation(Face);
    if (Now < NextHoldRepathTime) return;
    NextHoldRepathTime = Now + 1.5f;
    // 현재 각도에서 40° 돌린 링 위 지점 — 주기마다 같은 방향으로 돌아 회전하듯 보인다.
    const FVector ToMe = (E->GetActorLocation() - T->GetActorLocation()).GetSafeNormal2D();
    const FVector Dir = ToMe.RotateAngleAxis(40.f * HoldSide, FVector::UpVector);
    const FVector Want = T->GetActorLocation() + Dir * HoldDistance;
    FNavLocation Dest;
    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
    if (Nav && Nav->GetRandomReachablePointInRadius(Want, 120.f, Dest))
    {
        SetRunning(false);
        MoveToLocation(Dest.Location, 50.f);
    }
    else
    {
        HoldSide = -HoldSide;  // 막히면 반대로
    }
}

void AEnemyAIController::TickWithoutTarget(AEnemyCharacter* E, float Now)
{
    const FVector MyLoc = E->GetActorLocation();

    if (State == EEnemyState::Return)
    {
        if (FVector::Dist2D(MyLoc, Home) < 150.f)
        {
            State = EEnemyState::Wander;
            NextWanderTime = Now + WanderInterval;
        }
        else if (GetMoveStatus() != EPathFollowingStatus::Moving)
        {
            MoveToLocation(Home, 80.f);
        }
        return;
    }

    // Wander — 주기마다 홈 반경 안 도달 가능한 랜덤 지점.
    State = EEnemyState::Wander;
    if (WanderRadius <= 0.f || Now < NextWanderTime) return;
    NextWanderTime = Now + WanderInterval * FMath::FRandRange(0.7f, 1.3f);

    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
    FNavLocation Dest;
    if (Nav && Nav->GetRandomReachablePointInRadius(Home, WanderRadius, Dest))
    {
        SetRunning(false);
        MoveToLocation(Dest.Location, 60.f);
    }
}
