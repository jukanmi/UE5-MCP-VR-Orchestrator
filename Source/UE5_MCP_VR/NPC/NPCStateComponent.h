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
