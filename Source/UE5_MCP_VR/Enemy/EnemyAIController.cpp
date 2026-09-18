#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyCharacter.h"
#include "Core/Interfaces/Entity.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AISense_Sight.h"

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

void AEnemyAIController::SetTarget(AActor* NewTarget)
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
    Target = NewTarget;
    bTargetVisible = false;                       // 등 뒤에서 맞았을 수 있음 — 추적하며 시야로 확인
    LastSeenTime = GetWorld()->GetTimeSeconds();  // LoseTargetTime 만큼 찾을 여유
    State = EEnemyState::Chase;
}

void AEnemyAIController::ClearTarget()
{
    Target = nullptr;
    bTargetVisible = false;
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
            Target = Actor;
            State = EEnemyState::Chase;
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

    // 포기 조건 — 시야 상실 지속 또는 리시 초과 → 걸어서 홈으로.
    const bool bLost = !bTargetVisible && (Now - LastSeenTime) > LoseTargetTime;
    if (bLost || FVector::Dist2D(MyLoc, Home) > LeashRadius)
    {
        ClearTarget();
        State = EEnemyState::Return;
        SetRunning(false);
        MoveToLocation(Home, 80.f);
        return;
    }

    const float Dist = FVector::Dist(MyLoc, T->GetActorLocation());
    if (Dist <= AttackRange)
    {
        // 사거리 안 — 정지, 타겟 바라보기, 쿨다운마다 공격.
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
    if (!bMovingToTarget)
    {
        SetRunning(true);
        MoveToActor(T, AttackRange * 0.7f);
    }
    State = EEnemyState::Chase;
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
