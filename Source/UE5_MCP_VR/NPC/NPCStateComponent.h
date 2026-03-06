#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Struct/CharacterAttributes.h"
#include "Struct/NPCActionTypes.h" // EFacialState
#include "NPCStateComponent.generated.h"

class ASmartNPCAIController;

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
    // --- Character Attributes (능력치 전체) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Stats")
    FCharacterAttributes CurrentStats;

    // --- Facial State (표정 상태) ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Facial")
    EFacialState CurrentFacialState = EFacialState::Neutral;

    // --- Current Action Tracking ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|State")
    EAction CurrentActionType = EAction::Idle;

public:
    // --- Posture State ---
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|State")
    bool bIsSit = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|State")
    bool bIsLie = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|State")
    bool bIsSleeping = false;

    // 언제 마지막으로 피격당했는지 기록 (TimeSeconds)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|State")
    float LastHitTime = 0.0f;

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    FCharacterAttributes GetCurrentStats() const
    { 
        return CurrentStats;
    }

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    float GetCurrentHealth() const
    { 
        return CurrentStats.Resources.Health;
    }

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    float GetCurrentMaxHealth() const
    { 
        return CurrentStats.Resources.MaxHealth;
    }

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    float GetCurrentHealthRatio() const
    { 
        return CurrentStats.Resources.Health / FMath::Max(1.f, CurrentStats.Resources.MaxHealth);
    }

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    EFacialState GetCurrentFacialState() const
    { 
        return CurrentFacialState;
    }

    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    void SetCurrentActionType(EAction NewActionType)
    { 
        CurrentActionType = NewActionType;
    }


    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    EAction GetCurrentActionType() const
    { 
        return CurrentActionType;
    }

    // --- Public API ---

    // [의도(Why)] 빈번한 동일 표정 갱신 호출로부터 Blackboard 및 애니메이션 시스템의 불필요한 트리거 오버헤드를 방지합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Facial")
    void SetFacialExpression(EFacialState NewExpression);

    // [의도(Why)] 장비 변경, 레벨업, 버프 등에 의한 스탯 변동 시 하위 파생치(전투, 이동속도)를 한 번에 동기화하여 불일치를 해소합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    void RefreshStats();

    // [의도(Why)] 데이터 상의 이동 속도(Stats)와 실제 엔진 물리(CharacterMovementComponent) 간의 속도를 일치시킵니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    void ApplyMovementSpeed();

    // [의도(Why)] NPC가 예상치 못한 위협을 감지할 때, 스스로의 '지각력(Perception)' 스탯에 기반해 즉각 대응할 수 기회를 부여합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Reflex")
    bool TryReflexAction(int32 Difficulty);

    // [의도(Why)] 피격 처리 및 방어력 연산 후 최종 데미지만 체력에 반영하여 사망(Death) 조건을 중앙 통제합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    float ApplyDamage(float DamageAmount);

    // [의도(Why)] 체력 저하나 피격 등 치명적 이벤트 발생 시, 다음 루프를 기다리지 않고 서버(LLM)에 즉각적인 상황 인지 요청을 보내기 위함입니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Cognition")
    void RequestEmergencyCognition(const FString& EventType, const FString& Description);

protected:
    virtual void BeginPlay() override;

private:
    // 캐싱: Owner의 AIController에서 Blackboard 접근 시 사용
    ASmartNPCAIController* GetOwnerAIController() const;
};
