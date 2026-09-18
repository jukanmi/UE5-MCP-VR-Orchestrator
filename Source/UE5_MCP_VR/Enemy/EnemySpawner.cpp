#include "Enemy/EnemySpawner.h"
#include "Enemy/EnemyCharacter.h"
#include "NPC/Subsystems/NPCManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

AEnemySpawner::AEnemySpawner()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AEnemySpawner::BeginPlay()
{
    Super::BeginPlay();

    if (!EnemyClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[EnemySpawner] %s: EnemyClass 비어 있음 — 비활성"), *GetName());
        return;
    }

    if (bFillOnBeginPlay)
    {
        for (int32 i = GetAliveCount(); i < MaxAlive; ++i)
        {
            if (TotalSpawnLimit > 0 && TotalSpawned >= TotalSpawnLimit) break;
            if (!SpawnOne()) break;
        }
    }

    GetWorldTimerManager().SetTimer(SpawnTimer, this, &AEnemySpawner::TrySpawn, SpawnInterval, true);
}

int32 AEnemySpawner::GetAliveCount() const
{
    int32 N = 0;
    for (const TWeakObjectPtr<AEnemyCharacter>& W : Alive)
    {
        if (W.IsValid() && !W->bIsDead) ++N;
    }
    return N;
}

void AEnemySpawner::TrySpawn()
{
    if (TotalSpawnLimit > 0 && TotalSpawned >= TotalSpawnLimit)
    {
        GetWorldTimerManager().ClearTimer(SpawnTimer);  // 총량 소진 — 더 돌 이유 없음
        return;
    }
    if (GetAliveCount() >= MaxAlive) return;

    // 플레이어 눈앞 팝인 방지.
    if (MinPlayerDistance > 0.f)
    {
        if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
        {
            if (FVector::Dist(Player->GetActorLocation(), GetActorLocation()) < MinPlayerDistance) return;
        }
    }
    SpawnOne();
}

bool AEnemySpawner::FindSpawnPoint(FVector& OutLocation) const
{
    const FVector Center = GetActorLocation();
    if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
    {
        FNavLocation NavLoc;
        if (SpawnRadius > 0.f && Nav->GetRandomReachablePointInRadius(Center, SpawnRadius, NavLoc))
        {
            OutLocation = NavLoc.Location;
            return true;
        }
        if (Nav->ProjectPointToNavigation(Center, NavLoc))
        {
            OutLocation = NavLoc.Location;
            return true;
        }
    }
    // 네비 없음 — 스포너 위치 그대로(빌더가 ground_z 로 놓았다는 전제).
    OutLocation = Center;
    return true;
}

AEnemyCharacter* AEnemySpawner::SpawnOne()
{
    if (!EnemyClass) return nullptr;

    FVector Loc;
    FindSpawnPoint(Loc);
    // 네비 지점은 바닥 — 캡슐 절반 높이만큼 띄워 바닥 관통 스폰 방지.
    if (const AEnemyCharacter* CDO = EnemyClass->GetDefaultObject<AEnemyCharacter>())
    {
        if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent())
            Loc.Z += Cap->GetScaledCapsuleHalfHeight() + 2.f;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    Params.Owner = this;
    const FRotator Rot(0.f, FMath::FRandRange(0.f, 360.f), 0.f);
    AEnemyCharacter* E = GetWorld()->SpawnActor<AEnemyCharacter>(EnemyClass, Loc, Rot, Params);
    if (!E) return nullptr;

    E->OnEnemyDied.AddDynamic(this, &AEnemySpawner::HandleEnemyDied);
    Alive.Add(E);
    ++TotalSpawned;
    UE_LOG(LogTemp, Log, TEXT("[EnemySpawner] %s: %s 스폰 (%d/%d 생존, 누적 %d)"),
        *GetName(), *E->EnemyID, GetAliveCount(), MaxAlive, TotalSpawned);
    return E;
}

void AEnemySpawner::HandleEnemyDied(AEnemyCharacter* Dead)
{
    ++TotalKilled;
    Alive.RemoveAll([](const TWeakObjectPtr<AEnemyCharacter>& W) { return !W.IsValid() || W->bIsDead; });

    if (!bFlagSent && KillsForFlag > 0 && TotalKilled >= KillsForFlag && !KillFlag.IsEmpty())
    {
        bFlagSent = true;
        if (UNPCManager* Manager = UNPCManager::Get(this))
        {
            Manager->SendStoryEvent(TEXT("flag"), KillFlag);
        }
        UE_LOG(LogTemp, Log, TEXT("[EnemySpawner] %s: 킬 %d 달성 → flag '%s'"), *GetName(), TotalKilled, *KillFlag);
    }
}
