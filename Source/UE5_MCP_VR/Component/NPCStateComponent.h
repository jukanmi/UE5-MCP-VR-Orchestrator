#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../AI/CharacterAttributes.h"
#include "../AI/NPCActionTypes.h" // EFacialState
#include "NPCStateComponent.generated.h"

class ASmartNPCAIController;

/**
 * NPC 상태 관리 컴포넌트 (NPC State Component).
 * - 캐릭터 능력치(Stats), 표정(Facial), 상태(Status) 등을 관리.
 * - SmartNPC에서 분리하여 재사용성과 가독성을 높임.
 * - 왜 컴포넌트인가: Stats는 NPC의 "존재"에 관한 데이터이며,
 *   ActionComponent와 독립적으로 동작해야 하므로 별도 관리.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCStateComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPCStateComponent();

    // --- Character Attributes (능력치 전체) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Stats")
    FCharacterAttributes CurrentStats;

    // --- Facial State (표정 상태) ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Facial")
    EFacialState CurrentFacialState = EFacialState::Neutral;

    // --- Current Action Tracking ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|State")
    FString CurrentActionID;

    // --- Posture State ---
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|State")
    bool bIsSit = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|State")
    bool bIsLie = false;

    // --- Public API ---

    /**
     * 표정 변경 (내부 상태 + Blackboard 동기화).
     * - 중복 호출 시 무시하여 불필요한 업데이트 방지.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC|Facial")
    void SetFacialExpression(EFacialState NewExpression);

    /**
     * 스탯 재계산 (Base Stats → Derived Stats).
     * - RecalculateCombatStats 호출 후 이동속도 적용.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    void RefreshStats();

    /**
     * 이동속도를 CharacterMovementComponent에 적용.
     * - CurrentStats.Movement 값을 기반으로 설정.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    void ApplyMovementSpeed();

    /**
     * 반사 행동 판정 (Difficulty vs Perception 기반 주사위 굴림).
     * @return true: 반사 성공, false: 실패
     */
    UFUNCTION(BlueprintCallable, Category = "NPC|Reflex")
    bool TryReflexAction(int32 Difficulty);

    /**
     * 데미지 처리 (Health 차감 + 사망 판정).
     * - Owner의 TakeDamage에서 위임받아 처리.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC|Stats")
    float ApplyDamage(float DamageAmount);

    /**
     * 긴급 인지 요청 (Emergency Cognition).
     * - 위험 상황 시 LLM에 긴급 판단을 요청하는 트리거.
     */
    UFUNCTION(BlueprintCallable, Category = "NPC|Cognition")
    void RequestEmergencyCognition(const FString& EventType, const FString& Description);

protected:
    virtual void BeginPlay() override;

private:
    // 캐싱: Owner의 AIController에서 Blackboard 접근 시 사용
    ASmartNPCAIController* GetOwnerAIController() const;
};
