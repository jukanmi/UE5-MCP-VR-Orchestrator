#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Core/CharacterAttributes.h"      // FNPCAttributes, FCharacterAttributesBase
#include "../Core/GameStateData.h"            // FPerceptionData
#include "Struct/NPCActionTypes.h"             // EFacialState
#include "NPCStateComponent.generated.h"

class ASmartNPCAIController;
class ASmartNPC;

/**
 * NPC 행동 계획 (Multi-NPC Cached Planning).
 * [의도(Why)] "비싸게 계획 1회, 싸게 실행 N회". 풀 파이프라인(12B)이 산출한 plan 을 C++ 에
 *  영속 저장하고, 재계획 불필요 시 이 plan 을 prompt 에 실어 e4b 단독 경량 루프로 대사를 전개한다.
 *  Python Dict {goal, steps, relation_snapshot} 와 1:1 대응 (CLAUDE.md §1·§5 직렬화 정합).
 */
USTRUCT(BlueprintType)
struct FNPCPlan
{
    GENERATED_BODY()

    // 이 NPC 가 향후 몇 턴간 달성하려는 짧은 목표.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Plan")
    FString Goal;

    // 목표 달성을 위한 순서 있는 비트(beat) 목록.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Plan")
    TArray<FString> Steps;

    // 계획 수립 시점의 player 호감도 스냅샷 (affinity score 만 — 확정 결정).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Plan")
    int32 RelationSnapshot = 0;

    // plan 이 실제로 수립되었는지 (빈 plan 과 구분). false 면 송신 시 current_plan 생략.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Plan")
    bool bIsValid = false;
};

// plan 갱신 시 브로드캐스트 — 머리 위 plan 위젯(WBP)이 바인딩해 Goal 갱신. SetCurrentPlan 에서만 발화.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlanUpdated, const FNPCPlan&, NewPlan);

/**
 * NPC 상태 관리 컴포넌트 (NPC State Component).
 * [의도(Why)] NPC의 존재(속성, 상태, 감정) 자체를 하나의 컴포넌트로 응집시켜 액션(Action) 컴포넌트와의 결합도를 낮추고 재사용성을 극대화합니다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCStateComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPCStateComponent();

protected:
    // --- Facial State (표정 상태) ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Facial")
    EFacialState CurrentFacialState = EFacialState::Neutral;

    // --- Behavior Mode (행동 모드: Common/Combat) ---
    // [소유권] BehaviorMode 의 단일 소유자. ExecuteActionBatch(Batch.Mode)가 갱신하고
    // StateTree(STTask)가 GetBehaviorMode()로 읽는다. ActionComponent는 read 위임만.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|State")
    ENPCBehaviorMode CurrentBehaviorMode = ENPCBehaviorMode::Common;

public:
    // --- Posture State ---
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|State")
    bool bIsSit = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|State")
    bool bIsLie = false;

    // 언제 마지막으로 피격당했는지 기록 (TimeSeconds)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|State")
    float LastHitTime = 0.0f;

    // --- Helper: Owner Attributes Access ---
    FNPCAttributes GetAttributes() const;
    FNPCAttributes& GetMutableAttributes();

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    EFacialState GetCurrentFacialState() const
    { 
        return CurrentFacialState;
    }

    UFUNCTION(BlueprintCallable, Category = "NPC|State")
    void SetBehaviorMode(ENPCBehaviorMode NewMode)
    {
        CurrentBehaviorMode = NewMode;
    }

    UFUNCTION(BlueprintCallable, Category = "NPC|State")
    ENPCBehaviorMode GetBehaviorMode() const
    {
        return CurrentBehaviorMode;
    }

    // --- Public API ---

    UFUNCTION(BlueprintCallable, Category = "NPC|Facial")
    void SetFacialExpression(EFacialState NewExpression);

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    void RefreshStats();

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    void ApplyMovementSpeed();

    // [의도(Why)] NPC가 예상치 못한 위협을 감지할 때, 스스로의 '지각력(Perception)' 스탯에 기반해 즉각 대응할 수 기회를 부여합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Reflex")
    bool TryReflexAction(int32 Difficulty);

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    float ApplyDamage(float DamageAmount);

    UFUNCTION(BlueprintCallable, Category = "NPC|Cognition")
    void RequestEventCognition(const FPerceptionData& Perception);

    // --- Affinity (호감도) ---
    
    // [의도(Why)] 파이썬 서버가 계산한 타겟과의 호감도(Affinity)를 로컬 캐싱하여, 퍼셉션(시각/청각) 이벤트 발생 시 대상에 대한 즉각적인 위험도(Multiplier) 판단에 사용합니다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Relations")
    TMap<FString, int32> AffinityCache;

    // 에디터에서 디자이너가 튜닝 가능한 호감도 임계값
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "NPC|Relations")
    int32 AffinityFriendlyThreshold = 30;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "NPC|Relations")
    int32 AffinityHostileThreshold = -30;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "NPC|Relations")
    float AffinityDefaultMultiplier = 0.5f;

    // NPCManager 등이 서버로부터 호감도 업데이트를 받을 때 호출
    UFUNCTION(BlueprintCallable, Category = "NPC|Relations")
    void UpdateAffinity(const FString& TargetID, int32 NewScore);

    // 타겟 ID를 기반으로 호감도에 따른 위험도 배율 반환 (아군: 0.0, 적군: 1.0, 중립: 0.5)
    UFUNCTION(BlueprintCallable, Category = "NPC|Relations")
    float GetAffinityMultiplier(const FString& TargetID) const;

    /** Perception danger 단일 계산 진입점 = BaseDanger × 호감도배율.
     *  컨트롤러의 Sight/Hearing/PerceptionTick 모두 이 함수로 위협도 산출 (중복 계산 제거). */
    UFUNCTION(BlueprintCallable, Category = "NPC|Relations")
    float ComputePerceptionDanger(float BaseDanger, const FString& TargetID) const
    {
        return BaseDanger * GetAffinityMultiplier(TargetID);
    }

    // --- Cached Planning (계획 캐싱) ---

    // [소유권] 이 NPC 가 보관 중인 plan. Python 풀 파이프라인 응답(NpcPlans)으로 갱신,
    //  e4b 경량 루프 prompt 빌드 시 current_plan 으로 송신. NPCStateComponent 단일 소유.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Plan")
    FNPCPlan CurrentPlan;

    // 새 위협(danger ≥ 임계) 감지 시 SmartNPCAIController 가 세움 → 다음 prompt 에서 강제 재계획.
    // Combat 첫 진입 시만 세움 — 이미 Combat 중이면 무시(연속 perception tick 재계획 폭주 방지).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Plan")
    bool bDangerReplanPending = false;

    // e4b Stage1 이 plan goal 달성 감지 시 세움 → 다음 prompt 강제 재계획(새 plan 생성).
    // SetCurrentPlan 호출 시 자동 리셋.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Plan")
    bool bPlanAchievedPending = false;

    // plan 갱신 통지 — 머리 위 plan 위젯(WBP)이 GetStateComponent()->OnPlanUpdated 바인딩.
    UPROPERTY(BlueprintAssignable, Category = "NPC|Plan")
    FOnPlanUpdated OnPlanUpdated;

    UFUNCTION(BlueprintCallable, Category = "NPC|Plan")
    const FNPCPlan& GetCurrentPlan() const { return CurrentPlan; }

    // 재계획 응답 수신 시 호출 — plan 저장 + danger·achieved 플래그 리셋.
    UFUNCTION(BlueprintCallable, Category = "NPC|Plan")
    void SetCurrentPlan(const FNPCPlan& NewPlan)
    {
        CurrentPlan = NewPlan;
        CurrentPlan.bIsValid = true;
        bDangerReplanPending = false;
        bPlanAchievedPending = false;
        OnPlanUpdated.Broadcast(CurrentPlan);
    }

    // SmartNPCAIController 가 perception 에서 danger ≥ 임계 감지 시 호출 → 다음 prompt 강제 재계획.
    UFUNCTION(BlueprintCallable, Category = "NPC|Plan")
    void FlagDangerReplan() { bDangerReplanPending = true; }

    // e4b 가 plan 달성 감지 시 NPCManager 가 호출 → 다음 prompt 강제 재계획(새 plan 생성).
    UFUNCTION(BlueprintCallable, Category = "NPC|Plan")
    void FlagPlanAchieved() { bPlanAchievedPending = true; }

    /** 재계획 필요 판정: plan 없음 OR 전투 전환 감지 OR e4b plan 달성 신호.
     *  시간 기반(TurnsSinceReplan) 강제 재계획 제거 — plan 있으면 e4b 단독 유지. */
    UFUNCTION(BlueprintCallable, Category = "NPC|Plan")
    bool ShouldReplan() const
    {
        return !CurrentPlan.bIsValid
            || bDangerReplanPending
            || bPlanAchievedPending;
    }

protected:
    virtual void BeginPlay() override;

private:
    // 캐싱: Owner의 AIController에서 Blackboard 접근 시 사용
    ASmartNPCAIController* GetOwnerAIController() const;

    // --- Event Debounce ---
    FTimerHandle EventDebounceTimer;
    TArray<FPerceptionData> LocalEventQueue;

    UFUNCTION()
    void FlushEventReport();
};
