#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "EnemyAIController.generated.h"

class AEnemyCharacter;

/**
 * 필드 적 AI — 일반 게임식 FSM(틱 0.15초). BT/StateTree 에셋 없이 C++ 만으로 동작.
 *   Wander: 홈(스폰 지점) 주변 랜덤 지점 배회
 *   Chase : 타겟 추적(달리기) → AttackRange 안이면 정지·바라보기·공격(쿨다운)
 *           같은 타겟을 이미 MaxAttackers 명이 치고 있으면 Hold — 타겟 주위 HoldDistance 링을 돌며 자리가 나길 기다림(포위)
 *   Flee  : HP 가 FleeHealthPct 아래로 처음 떨어지면 타겟 반대편으로 FleeDuration 초 도주 후 재교전
 *   Return: 시야 상실 LoseTargetTime 경과 또는 홈에서 LeashRadius 초과 → 걸어서 홈 복귀
 * 타겟 획득: 시야에 잡힌 플레이어(자동) / 자기를 때린 누구든(EnemyCharacter::TakeDamage → SetTarget).
 * 타겟을 잡으면 AlertRadius 안 다른 적에게 전파(무리 경보) — 한 명이 보면 캠프 전체가 온다.
 * 시야만으로 아군 NPC 를 먼저 치지 않는다 — 마을 NPC(Guard·James 등)가 학살당해 스토리가 막히는 걸 막기 위함.
 */
UCLASS()
class UE5_MCP_VR_API AEnemyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AEnemyAIController();

    /** 외부 타겟 지정(피격 반격·무리 경보). 사망·같은 적 종류·비폰은 무시. 이미 타겟이 있으면 더 가까운 쪽만 교체.
     *  bAlertOthers: AlertRadius 안 동료에게 전파(전파받은 쪽은 false 로 받아 연쇄 폭주 방지). */
    void SetTarget(AActor* NewTarget, bool bAlertOthers = true);

    /** 이 타겟의 공격 슬롯을 쥐고 있는가(사거리 진입 시 획득, Hold·포기·사망 시 반납) — 슬롯 집계용. */
    bool IsEngaging(const AActor* T) const;

    /** 폰 사망 — 이동 정지·타겟 해제·틱 종료. UnPossess 는 하지 않는다(폰 Destroy 가 컨트롤러도 정리). */
    void OnPawnDied();

    /** 테스트·디버그용 한 줄 상태: "Chase eng=1 vis=1 tgt=BP_VRPawn_C_0 d=153". */
    UFUNCTION(BlueprintCallable, Category = "AI|Debug")
    FString GetDebugState() const;

    // --- 시야 ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Sight")
    float SightRadius = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Sight")
    float LoseSightRadius = 2600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Sight")
    float SightAngle = 70.f;

    // --- 행동 ---
    /** 홈 주변 배회 반경(cm). 0 이면 제자리 대기. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float WanderRadius = 800.f;

    /** 배회 목적지 갱신 주기(초, ±30% 지터). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float WanderInterval = 4.f;

    /** 홈에서 이 이상 멀어지면 추적 포기·복귀(cm). 플레이어가 마을까지 끌고 가는 카이팅 방지. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float LeashRadius = 3500.f;

    /** 타겟이 시야 밖에 이만큼 머물면 포기(초). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float LoseTargetTime = 6.f;

    /** 이 거리 안이면 정지 후 공격 시작(cm). 캐릭터 AttackHitRange(200) 보다 작게. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float AttackRange = 170.f;

    // --- 지능 ---
    /** 타겟 획득 시 이 반경(cm) 안 동료에게 전파. 0=끔. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Smart")
    float AlertRadius = 1500.f;

    /** 한 타겟을 동시에 치는 상한. 초과분은 Hold(포위 대기). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Smart", meta = (ClampMin = "1"))
    int32 MaxAttackers = 2;

    /** Hold 시 타겟과 유지하는 거리(cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Smart")
    float HoldDistance = 380.f;

    // 도주 임계·지속은 종류별로 달라(보스 0) AEnemyCharacter::FleeHealthPct/FleeDuration 이 가진다.

protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION()
    void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UAIPerceptionComponent* PerceptionComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
    UAISenseConfig_Sight* SightConfig;

private:
    enum class EEnemyState : uint8 { Wander, Chase, Hold, Flee, Return };
    EEnemyState State = EEnemyState::Wander;

    TWeakObjectPtr<AActor> Target;
    bool bTargetVisible = false;
    float LastSeenTime = -1000.f;
    float NextWanderTime = 0.f;
    float NextAttackTime = 0.f;
    float NextHoldRepathTime = 0.f;
    float FleeUntil = 0.f;
    bool bHasFled = false;   // 생애 1회
    bool bEngaged = false;   // 공격 슬롯 보유 — 거리로 판정하면 틱 사이(0.15s×415cm/s) 창을 건너뛰어 3명이 다 붙는다(실측)
    float HoldSide = 1.f;    // 포위 회전 방향(±1)
    FVector Home = FVector::ZeroVector;

    AEnemyCharacter* GetEnemy() const;
    bool IsValidTarget(const AActor* Actor) const;
    void ClearTarget();
    void SetRunning(bool bRun);
    void TickWithTarget(AEnemyCharacter* Enemy, AActor* T, float Now);
    void TickWithoutTarget(AEnemyCharacter* Enemy, float Now);
    void AlertNearby(AActor* T);
    int32 CountEngaging(const AActor* T) const;
    void StartFlee(AEnemyCharacter* Enemy, AActor* T, float Now);
    void HoldAround(AEnemyCharacter* Enemy, AActor* T, float Now);
};
