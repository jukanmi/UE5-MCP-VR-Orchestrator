#include "Villager/VillagerAIController.h"
#include "Villager/VillagerCharacter.h"
#include "Enemy/EnemyCharacter.h"
#include "NPC/Components/NPCRagdollComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AISense_Sight.h"

AVillagerAIController::AVillagerAIController()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.2f;  // 판단 주기 — 적(0.15)보다 느긋해도 된다(공격 사거리 판정 없음)

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
        PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &AVillagerAIController::OnTargetPerceptionUpdated);
    }
}

void AVillagerAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    Home = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
    Threat = nullptr;
    bThreatVisible = false;
    StartIdle(GetWorld()->GetTimeSeconds());

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

AVillagerCharacter* AVillagerAIController::GetVillager() const
{
    return Cast<AVillagerCharacter>(GetPawn());
}

bool AVillagerAIController::IsSightThreat(const AActor* Actor)
{
    return Actor && Actor->IsA<AEnemyCharacter>() && !ACombatCharacter::IsActorDead(Actor);
}

void AVillagerAIController::SetRunning(bool bRun)
{
    AVillagerCharacter* V = GetVillager();
    if (!V) return;
    if (UCharacterMovementComponent* CMC = V->GetCharacterMovement())
    {
        CMC->MaxWalkSpeed = bRun ? V->Attributes.Movement.RunSpeed : V->Attributes.Movement.WalkSpeed;
    }
}

void AVillagerAIController::StartIdle(float Now)
{
    State = EVillagerState::Idle;
    NextWanderTime = Now + FMath::FRandRange(IdleTimeMin, FMath::Max(IdleTimeMin, IdleTimeMax));
}

FString AVillagerAIController::GetDebugState() const
{
    static const TCHAR* Names[] = { TEXT("Idle"), TEXT("Wander"), TEXT("Greet"), TEXT("Flee"), TEXT("Return"), TEXT("Dead") };
    const AActor* T = Threat.Get();
    const APawn* Me = GetPawn();
    const float D = (T && Me) ? FVector::Dist(Me->GetActorLocation(), T->GetActorLocation()) : -1.f;
    const float H = Me ? FVector::Dist2D(Me->GetActorLocation(), Home) : -1.f;
    return FString::Printf(TEXT("%s thr=%s vis=%d d=%.0f move=%d home=%.0f"), Names[(int32)State],
        *GetNameSafe(T), bThreatVisible, D, (int32)GetMoveStatus(), H);
}

void AVillagerAIController::OnPawnDied()
{
    StopMovement();
    Threat = nullptr;
    State = EVillagerState::Dead;
    SetActorTickEnabled(false);
}

// ============================================================================
// 위협 획득 — 시야(적만) / 피격(누구든)
// ============================================================================
void AVillagerAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (!Actor || Actor == GetPawn()) return;
    const float Now = GetWorld()->GetTimeSeconds();

    // 현재 위협(플레이어 포함)의 가시성 갱신 — 위협 자동 획득은 적만.
    if (Threat.Get() == Actor)
    {
        bThreatVisible = Stimulus.WasSuccessfullySensed();
        LastThreatTime = Now;
        return;
    }
    if (!Stimulus.WasSuccessfullySensed() || !IsSightThreat(Actor)) return;

    // 이미 도망 중이면 더 가까운 적으로만 갈아탄다.
    const APawn* Me = GetPawn();
    if (AActor* Cur = Threat.Get(); Cur && Me
        && FVector::DistSquared(Me->GetActorLocation(), Cur->GetActorLocation())
            <= FVector::DistSquared(Me->GetActorLocation(), Actor->GetActorLocation()))
    {
        return;
    }

    Threat = Actor;
    bThreatVisible = true;
    LastThreatTime = Now;
    UE_LOG(LogTemp, Log, TEXT("[VillagerAI] %s 적 발견 %s → 도주"), *GetVillager()->VillagerID, *GetNameSafe(Actor));
}

void AVillagerAIController::OnThreat(AActor* NewThreat)
{
    AVillagerCharacter* V = GetVillager();
    if (!V || V->bIsDead || !NewThreat || NewThreat == V || !NewThreat->IsA<APawn>()) return;
    if (ACombatCharacter::IsActorDead(NewThreat)) return;
    const float Now = GetWorld()->GetTimeSeconds();
    Threat = NewThreat;
    bThreatVisible = PerceptionComp && PerceptionComp->HasActiveStimulus(*NewThreat, UAISense::GetSenseID<UAISense_Sight>());
    LastThreatTime = Now;
    UE_LOG(LogTemp, Log, TEXT("[VillagerAI] %s 피격 by %s → 도주"), *V->VillagerID, *GetNameSafe(NewThreat));
    FleeFrom(V, NewThreat, Now);
}

// ============================================================================
// FSM 틱
// ============================================================================
void AVillagerAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    AVillagerCharacter* V = GetVillager();
    if (!V || V->bIsDead) return;
    const float Now = GetWorld()->GetTimeSeconds();

    AActor* T = Threat.Get();
    if (T && ACombatCharacter::IsActorDead(T))
    {
        Threat = nullptr;
        T = nullptr;
    }

    if (T)
    {
        // 등 돌리고 달리면 시야(FOV)에선 바로 사라지지만 코앞의 위협은 안다 — LoseSightRadius 안이면 본 것으로 친다.
        // 넘어져 있는 동안(래그돌)은 시계를 멈춘다 — 일어난 뒤에 도망칠 시간을 준다.
        const bool bNear = FVector::Dist(V->GetActorLocation(), T->GetActorLocation()) <= LoseSightRadius;
        if (bNear || (V->RagdollComponent && V->RagdollComponent->IsKnockedDown())) LastThreatTime = Now;
        // 보이거나 마지막 목격 후 FleeCalmTime 안이면 계속 도주(주기마다 위협 현재 위치 기준 재경로).
        if (bThreatVisible || (Now - LastThreatTime) <= FleeCalmTime)
        {
            if (State != EVillagerState::Flee || Now >= NextFleeRepath) FleeFrom(V, T, Now);
            return;
        }
        UE_LOG(LogTemp, Log, TEXT("[VillagerAI] %s 진정 → 홈 복귀"), *V->VillagerID);
        Threat = nullptr;
        State = EVillagerState::Return;
        SetRunning(false);
        MoveToLocation(Home, 80.f);
        return;
    }

    if (State == EVillagerState::Flee)
    {
        // 위협이 죽었거나 사라졌다(Destroy) → 걸어서 홈 복귀. 없으면 Flee 에 영영 갇힌다.
        UE_LOG(LogTemp, Log, TEXT("[VillagerAI] %s 위협 소멸 → 홈 복귀"), *V->VillagerID);
        State = EVillagerState::Return;
        SetRunning(false);
        MoveToLocation(Home, 80.f);
        return;
    }

    if (V->IsPlayingOneShot()) return;  // 인사·피격 클립 중엔 서 있기

    switch (State)
    {
    case EVillagerState::Return:
        if (FVector::Dist2D(V->GetActorLocation(), Home) > 150.f)
        {
            if (GetMoveStatus() != EPathFollowingStatus::Moving) MoveToLocation(Home, 80.f);
            return;
        }
        StartIdle(Now);
        return;

    case EVillagerState::Greet:  // 손 흔들기 끝
        StartIdle(Now);
        return;

    case EVillagerState::Idle:
    {
        if (TryGreet(V, Now) || Now < NextWanderTime) return;
        if (V->WanderRadius <= 0.f)
        {
            StartIdle(Now);
            return;
        }
        UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
        FNavLocation Dest;
        if (Nav && Nav->GetRandomReachablePointInRadius(Home, V->WanderRadius, Dest))
        {
            SetRunning(false);
            MoveToLocation(Dest.Location, 60.f);
            State = EVillagerState::Wander;
        }
        else
        {
            StartIdle(Now);
        }
        return;
    }

    case EVillagerState::Wander:
        if (TryGreet(V, Now)) return;
        if (GetMoveStatus() != EPathFollowingStatus::Moving) StartIdle(Now);
        return;

    default:
        return;
    }
}

bool AVillagerAIController::TryGreet(AVillagerCharacter* V, float Now)
{
    if (Now < NextGreetTime) return false;
    APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!P || ACombatCharacter::IsActorDead(P)) return false;
    if (FVector::DistSquared(P->GetActorLocation(), V->GetActorLocation()) > GreetRadius * GreetRadius) return false;

    DoGreet(V, P, Now, /*bWave=*/true);
    return true;
}

bool AVillagerAIController::GreetNow(AActor* Player, bool bWave)
{
    AVillagerCharacter* V = GetVillager();
    if (!V || V->bIsDead || !Player) return false;
    if (State == EVillagerState::Flee || State == EVillagerState::Return || State == EVillagerState::Dead) return false;
    DoGreet(V, Player, GetWorld()->GetTimeSeconds(), bWave);
    return true;
}

void AVillagerAIController::DoGreet(AVillagerCharacter* V, const AActor* P, float Now, bool bWave)
{
    NextGreetTime = Now + GreetCooldown;
    StopMovement();
    FRotator Face = (P->GetActorLocation() - V->GetActorLocation()).Rotation();
    Face.Pitch = 0.f;
    Face.Roll = 0.f;
    V->SetActorRotation(Face);
    const float Len = bWave ? V->PlayWave() : 0.f;  // 클립 없으면 0 → 다음 틱 바로 Idle
    State = EVillagerState::Greet;
    UE_LOG(LogTemp, Log, TEXT("[VillagerAI] %s 인사(Wave %.1fs) → %s"), *V->VillagerID, Len, *GetNameSafe(P));
}

void AVillagerAIController::FleeFrom(AVillagerCharacter* V, const AActor* T, float Now)
{
    if (State != EVillagerState::Flee)
    {
        UE_LOG(LogTemp, Log, TEXT("[VillagerAI] %s 도주 from %s"), *V->VillagerID, *GetNameSafe(T));
    }
    State = EVillagerState::Flee;
    NextFleeRepath = Now + 1.5f;
    SetRunning(true);

    // 위협 반대 방향 FleeDistance 근처 도달 가능 지점. 막혔으면 절반 거리.
    UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
    if (!Nav) return;
    const FVector MyLoc = V->GetActorLocation();
    const FVector TLoc = T->GetActorLocation();
    const FVector Away = (MyLoc - TLoc).GetSafeNormal2D();
    FNavLocation Dest;
    for (const float Frac : { 1.0f, 0.5f })
    {
        if (Nav->GetRandomReachablePointInRadius(MyLoc + Away * FleeDistance * Frac, 400.f, Dest))
        {
            MoveToLocation(Dest.Location, 60.f);
            return;
        }
    }
    // 반대편이 벽·가판대로 막힘 — 주변 랜덤 지점 중 위협에서 가장 먼 곳. 홈은 안 된다(위협이 보통 홈 옆에 있다 → 되돌아 뛰는 실측).
    float BestD2 = FVector::DistSquared2D(MyLoc, TLoc) + 200.f * 200.f;
    bool bFound = false;
    for (int32 i = 0; i < 6; ++i)
    {
        FNavLocation Cand;
        if (Nav->GetRandomReachablePointInRadius(MyLoc, FleeDistance, Cand) && FVector::DistSquared2D(Cand.Location, TLoc) > BestD2)
        {
            BestD2 = FVector::DistSquared2D(Cand.Location, TLoc);
            Dest = Cand;
            bFound = true;
        }
    }
    if (bFound) MoveToLocation(Dest.Location, 60.f);
    else StopMovement();  // 갈 데가 없다 — 구석에서 떤다
}
