#pragma once

#include "CoreMinimal.h"
#include "Core/BP/CombatCharacter.h"  // 피격/타격 공통 베이스(bIsDead·공격 판정 윈도우·부위 배율)
#include "Engine/TimerHandle.h"
#include "Core/Interfaces/Entity.h"  // INPCEntity → ICharacterEntity → IGameplayTagAssetInterface 포함
#include "NPC/Components/NPCStateComponent.h"

#include "SmartNPC.generated.h"

class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;
class UAIPerceptionComponent;
class UAIPerceptionStimuliSourceComponent;
class UStateTree;
class UBlackboardData;
class UWidgetComponent;
class UNPCDialogueUIComponent;
class UNPCRagdollComponent;
class UAnimMontage;
class USoundBase;
struct FActionBatch;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCDied, ASmartNPC*, DeadNPC);

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API ASmartNPC : public ACombatCharacter, public INPC
{
	GENERATED_BODY()

public:
    // --- IGameplayTagAssetInterface 구현 ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	void AddStateTag(FGameplayTag Tag);
	void RemoveStateTag(FGameplayTag Tag);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tags")
    FGameplayTagContainer GameplayTags;
    ASmartNPC();

    UFUNCTION(BlueprintCallable, Category = "MCP|Components")
	UNPCStateComponent* GetStateComponent() const { return StateComponent; }

    UFUNCTION(BlueprintCallable, Category = "MCP|Components")
	UNPCActionComponent* GetActionComponent() const { return ActionComponent; }

    UFUNCTION(BlueprintCallable, Category = "MCP|Components")
	UNPCInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

    // === Components ===

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
	UNPCStateComponent* StateComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
	UNPCActionComponent* ActionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
	UNPCInventoryComponent* InventoryComponent;

    // 액티브 래그돌(Flinch/Knockdown/사망 래그돌) — 튜닝값·진행 상태 전부 컴포넌트 소유. PA_SmartNPC(Physics Asset) 필요.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Ragdoll")
    UNPCRagdollComponent* RagdollComponent;

    // 플레이어 근접 공격을 쳐냈을 때(RNG 패링 성공) 재생할 3D 사운드 — 미배정이면 조용히 스킵.
    UPROPERTY(EditDefaultsOnly, Category = "MCP|Combat")
    USoundBase* ParrySound = nullptr;

    // === Identity ===

    /** NPC 고유 ID (예: "Guard_1", "Merchant_A") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    FString AgentID;

    /** perception·호감도·승리 보고에 쓰는 대상 식별자 — 전투 캐릭터는 GetCombatId(SmartNPC=AgentID,
     *  EnemyCharacter=EnemyID), 그 외(플레이어 폰 등)는 액터 이름.
     *  객체 이름(BP_SmartNPC_C_UAID_…)은 레벨 재배치마다 바뀌어 호감도 행·스토리 boss_id 가 어긋난다. */
    static FString PerceptionIdFor(const AActor* Actor);
    virtual FString GetCombatId() const override { return AgentID; }

    /** 이 NPC가 사용할 StateTree 에셋 (StateTreeAISchema). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    UStateTree* StateTreeAsset;

    /** Blackboard 데이터 (Perception → TargetActor 등 공유 키 정의). BB_NPC.uasset 그대로 재사용. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    UBlackboardData* BlackboardAsset;

    // === Vision & Hearing Config (AIController에 적용됨) ===

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightRadius = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float LoseSightRadius = 3500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightAngle = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Hearing")
    float HearingRange = 3000.0f;

    // === Perception Source ===
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|AI|Perception")
	UAIPerceptionStimuliSourceComponent* StimuliSource;

    // === Dialogue Subtitle (머리 위 WorldSpace 말풍선) ===

    /** 말풍선 컴포넌트 — 자막 텍스트·표시 타이머 전부 이쪽 소유.
     *  액터는 "무엇을 말할지"만 넘기고 "언제까지 띄울지"는 관여하지 않는다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Dialogue")
    UNPCDialogueUIComponent* DialogueWidgetComp;

    /** 머리 위 말풍선에 대사 표시 — 말풍선 컴포넌트로 넘긴다.
     *  래퍼를 남긴 이유: 호출부(NPCManager·BP)가 NPC 를 통해 말을 거는 형태를 유지하기 위함. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void ShowSubtitle(const FString& Text);

    /** 말풍선 숨김 + 텍스트 클리어. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void HideSubtitle();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // === Death === (bIsDead 는 ACombatCharacter)

    /** 사망 시 브로드캐스트. BP에서 사망 애니메이션·VFX 연결용. */
    UPROPERTY(BlueprintAssignable, Category = "MCP|State")
    FOnNPCDied OnNPCDied;

    // === Damage Hook (UE5 Actor Override) ===

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        class AController* EventInstigator, AActor* DamageCauser) override;


    // === Stats ===

    /** NPC 전용 능력치 데이터 (BehavioralTraits 포함). 이 구조체 하나로 모든 스탯/행동 특성을 관리합니다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    FNPCAttributes NPCAttributes;


    // === IEntity / INPCEntity 인터페이스 구현 ===

    virtual FString GetEntityID_Implementation() const override { return AgentID; }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::NPC; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }

    // ICharacterBase
    virtual FCharacterAttributesBase GetAttributes_Implementation() const override { return NPCAttributes; }
    virtual void ApplyResourceDelta_Implementation(float DeltaHealth, float DeltaMana, float DeltaStamina) override
    { NPCAttributes.Resources.ApplyDelta(DeltaHealth, DeltaMana, DeltaStamina); }
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const override;

    // INPC
    virtual FString GetAgentID_Implementation() const override { return AgentID; }
    virtual FNPCAttributes GetNPCAttributes_Implementation() const override { return NPCAttributes; }
    void ExecuteActionBatch(const FActionBatch& Batch);
    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void OnActionCompleted();


    /** HP가 0 이하로 떨어졌을 때 호출. 액션 중지 → NPCMap 퇴출 → AI 해제 → 일정 시간 후 Actor 제거. */
    UFUNCTION(BlueprintCallable, Category = "MCP|State")
    void HandleDeath();

    // 공격 판정(SetCurrentAttackTarget/PerformAttackHit·거리·arc 게이트)은 ACombatCharacter.
    // ExecuteAttackAction 이 LLM 지정 타겟을 세팅하고 AM_Attack 의 노티파이가 소비.
    // 데미지 값 = NPCAttributes.Combat.AttackPower × AttackDamageScale (몽타주라 스윙속도 없어 고정).

    /** AttackPower → 실데미지 환산 배율. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    float AttackDamageScale = 1.0f;

protected:
    virtual float ComputeAttackDamage() const override { return NPCAttributes.Combat.AttackPower * AttackDamageScale; }

private:
    void DestroyAfterDeath();
};
