#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemySpawner.generated.h"

class AEnemyCharacter;

/**
 * 필드 적 주기 스포너 — 서브퀘스트 토벌 대상을 지정 구역에 유지한다.
 * SpawnInterval 마다 생존 수 < MaxAlive 면 스포너 반경 안 네비 지점에 1기 스폰(플레이어가 MinPlayerDistance 안이면 보류 — 눈앞 팝인 방지).
 * TotalSpawnLimit 로 총량 제한(보스=1, 0=무제한). 누적 킬이 KillsForFlag 에 닿으면 story flag 1회 송신
 * → 사이드퀘스트 complete_when {type: flag, name: KillFlag}. 보스는 EnemyCharacter 가 npc_died 로 알리므로 flag 불필요.
 * 레벨 배치는 tools/build_story_scene.py build_enemies() (SCN_ 라벨, 재생성 대상).
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API AEnemySpawner : public AActor
{
    GENERATED_BODY()

public:
    AEnemySpawner();

    /** 스폰할 적 BP 클래스(EnemyID·스탯·메시는 그 BP 가 가짐). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
    TSubclassOf<AEnemyCharacter> EnemyClass;

    /** 동시 생존 상한. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner", meta = (ClampMin = "1"))
    int32 MaxAlive = 3;

    /** 총 스폰 상한. 0=무제한(죽으면 계속 리스폰). 보스 스포너는 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner", meta = (ClampMin = "0"))
    int32 TotalSpawnLimit = 0;

    /** 스폰 시도 주기(초). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner", meta = (ClampMin = "1.0"))
    float SpawnInterval = 20.f;

    /** 스포너 중심에서 이 반경 안 도달 가능한 네비 지점에 스폰(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
    float SpawnRadius = 600.f;

    /** 플레이어가 이보다 가까우면 주기 스폰 보류(cm). 0=항상 스폰. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
    float MinPlayerDistance = 1500.f;

    /** BeginPlay 에 MaxAlive 까지 즉시 채움(거리 조건 무시). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
    bool bFillOnBeginPlay = true;

    /** 누적 킬이 이 수에 닿으면 KillFlag 송신(0=끔). 이후 스폰은 계속된다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Story", meta = (ClampMin = "0"))
    int32 KillsForFlag = 0;

    /** story_event flag 이름 — 사이드퀘스트 yaml complete_when.name 과 일치. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Story")
    FString KillFlag;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner|State")
    int32 TotalSpawned = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner|State")
    int32 TotalKilled = 0;

    /** 즉시 1기 스폰(상한·거리 무시). 실패 시 null. 디버그·BP 용. */
    UFUNCTION(BlueprintCallable, Category = "Spawner")
    AEnemyCharacter* SpawnOne();

    UFUNCTION(BlueprintCallable, Category = "Spawner")
    int32 GetAliveCount() const;

protected:
    virtual void BeginPlay() override;

private:
    TArray<TWeakObjectPtr<AEnemyCharacter>> Alive;
    FTimerHandle SpawnTimer;
    bool bFlagSent = false;

    void TrySpawn();
    bool FindSpawnPoint(FVector& OutLocation) const;

    UFUNCTION()
    void HandleEnemyDied(AEnemyCharacter* Dead);
};
