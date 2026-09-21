#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "VillagerAIController.generated.h"

class AVillagerCharacter;

/**
 * 앰비언트 주민 AI — 적 FSM(AEnemyAIController)에서 추적·공격을 빼고 인사를 넣은 형태. BT/StateTree 에셋 없이 C++ 틱(0.2초).
 *   Idle  : 제자리 IdleTimeMin~Max 초 대기
 *   Wander: 홈(배치 지점) WanderRadius 안 도달 가능한 랜덤 지점으로 걷기 → 도착하면 Idle
 *   Greet : 플레이어가 GreetRadius 안을 지나가면 멈춰 바라보고 Wave 1회(대사 없음) → Idle. GreetCooldown 쿨다운.
 *           플레이어 Interact·채팅(GreetNow)은 쿨다운 무시 — 대사는 폰(AVillagerCharacter)이 낸다.
 *   Flee  : 적(AEnemyCharacter)이 시야에 들어오거나 누구에게든 맞으면 위협 반대 방향으로 달리기(1.5초마다 재경로).
 *           위협을 마지막으로 본 뒤 FleeCalmTime 지나면 Return
 *   Return: 걸어서 홈 복귀 → Idle
 * 반격 없음. 서버·LLM 0.
 */
UCLASS()
class UE5_MCP_VR_API AVillagerAIController : public AAIController
{
    GENERATED_BODY()

public:
    AVillagerAIController();

    /** 위협 통보(피격) — 그쪽에서 즉시 도망. 비폰·사망·자기 자신은 무시. 플레이어도 위협이 된다(맞았으니). */
    void OnThreat(AActor* NewThreat);

    /** 폰 사망 — 이동 정지·틱 종료. UnPossess 는 하지 않는다(폰 Destroy 가 컨트롤러도 정리). */
    void OnPawnDied();

    /** 플레이어가 말 걸었다 — 쿨다운 무시하고 멈춰 바라보기(+bWave 면 손 흔들기). 도주·복귀·사망 중이면 false. */
    bool GreetNow(AActor* Player, bool bWave);

    /** 테스트·디버그용 한 줄 상태: "Wander thr=None vis=0 d=-1 move=1 home=312". */
    UFUNCTION(BlueprintCallable, Category = "AI|Debug")
    FString GetDebugState() const;

    // --- 시야 ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Sight")
    float SightRadius = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Sight")
    float LoseSightRadius = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Sight")
    float SightAngle = 90.f;

    // --- 행동 ---
    /** 아이들 지속(초) 범위 — 매번 랜덤. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float IdleTimeMin = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float IdleTimeMax = 8.f;

    /** 플레이어가 이 거리(cm) 안이면 자동 인사(Wave). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float GreetRadius = 200.f;

    /** 인사 재발동 대기(초). 말풍선·손 흔들기 남발 방지. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float GreetCooldown = 30.f;

    /** 도주 목적지 거리(cm) — 위협 반대 방향. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float FleeDistance = 1200.f;

    /** 위협을 마지막으로 본 뒤 이만큼(초) 지나면 진정·복귀. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Behavior")
    float FleeCalmTime = 5.f;

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
    enum class EVillagerState : uint8 { Idle, Wander, Greet, Flee, Return, Dead };
    EVillagerState State = EVillagerState::Idle;

    FVector Home = FVector::ZeroVector;
    TWeakObjectPtr<AActor> Threat;
    bool bThreatVisible = false;
    float LastThreatTime = -1000.f;
    float NextWanderTime = 0.f;
    float NextGreetTime = 0.f;
    float NextFleeRepath = 0.f;

    AVillagerCharacter* GetVillager() const;
    /** 시야만으로 위협이 되는 대상 — 살아 있는 적(AEnemyCharacter). 플레이어·아군은 때렸을 때만(OnThreat). */
    static bool IsSightThreat(const AActor* Actor);
    void SetRunning(bool bRun);
    void StartIdle(float Now);
    bool TryGreet(AVillagerCharacter* V, float Now);
    void DoGreet(AVillagerCharacter* V, const AActor* P, float Now, bool bWave);
    void FleeFrom(AVillagerCharacter* V, const AActor* T, float Now);
};
