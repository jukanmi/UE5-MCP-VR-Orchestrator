#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
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
struct FActionBatch;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCDied, ASmartNPC*, DeadNPC);

// [의도(Why)] 히트스캔이 채워주는 본 이름을 부위로 분류해 부위별 데미지 배율을 적용하기 위함.
UENUM(BlueprintType)
enum class EBodyPartType : uint8
{
    Torso     UMETA(DisplayName="몸통"),    // 배율 1.0 (기본·본 미식별 폴백)
    Head      UMETA(DisplayName="머리"),    // 배율 2.0
    ArmLeft   UMETA(DisplayName="왼팔"),    // 배율 0.75
    ArmRight  UMETA(DisplayName="오른팔"),  // 배율 0.75
    LegLeft   UMETA(DisplayName="왼다리"),  // 배율 0.75
    LegRight  UMETA(DisplayName="오른다리"),// 배율 0.75
};

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API ASmartNPC : public ACharacter, public INPC
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

    // === Identity ===

    /** NPC 고유 ID (예: "Guard_1", "Merchant_A") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    FString AgentID;

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

    // === Death ===

    /** 사망 여부. true이면 NPC맵에서 퇴출 완료 + 모든 액션 중지 상태. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|State")
    bool bIsDead = false;

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

    // ====================================================================
    // 공격 판정 (NPC→타겟) — AM_Attack 의 AnimNotifyState_NPCAttackHit 가 구동.
    // 데미지 값 = NPCAttributes.Combat.AttackPower × AttackDamageScale (몽타주라 스윙속도 없어 고정).
    // ====================================================================

    /** ExecuteAttackAction 이 LLM 지정 타겟을 저장. 노티파이 윈도우가 이 단일 타겟만 타격(친선사격 방지). */
    void SetCurrentAttackTarget(AActor* Target) { CurrentAttackTarget = Target; }

    /** 노티파이 윈도우 진입(NotifyBegin) — 스윙당 1회 가드 리셋. */
    void BeginAttackHitWindow() { bAttackHitConsumed = false; }

    /** 노티파이 윈도우 매 틱(NotifyTick) — 타겟이 거리·arc 게이트 통과 시 1회 데미지 적용. */
    void PerformAttackHit();

    /** 타격 유효 거리(cm) — 타겟이 이 안에 들어와야 명중. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    float AttackHitRange = 200.f;

    /** 타격 정면 arc 게이트 — forward·(타겟방향) 내적이 이 값 이상이어야 명중(0.3≈72°). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    float AttackHitArcCos = 0.3f;

    /** AttackPower → 실데미지 환산 배율. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    float AttackDamageScale = 1.0f;

    /** 플레이어 피격 시 넉백 속도(cm/s, LaunchCharacter XY). 0=넉백 끔. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    float NPCKnockbackSpeed = 400.f;

    /** 공격 판정 디버그 — 타겟·거리·명중을 화면/로그에 표시. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    bool bDebugAttackHit = false;

private:
    void DestroyAfterDeath();

    // --- 공격 판정 상태 (NPC→타겟) ---
    TWeakObjectPtr<AActor> CurrentAttackTarget;  // ExecuteAttackAction 이 세팅, 노티파이가 소비
    bool bAttackHitConsumed = false;             // 스윙당 1회 가드(NotifyBegin 리셋)



public:

    /** VR 플레이어 멜리 재타격 쿨다운용 — 마지막 피격 시각(World TimeSeconds). VRPawn 가 읽고 씀.
     *  소유자(NPC) 가 직접 보유 → NPC 소멸 시 함께 사라져 누적/만료정리 불필요. */
    float LastMeleeHitTime = -1000.f;
};
