#pragma once

#include "CoreMinimal.h"
#include "Core/BP/CombatCharacter.h"
#include "Core/Interfaces/Entity.h"
#include "EnemyCharacter.generated.h"

class UAIPerceptionStimuliSourceComponent;
class UNPCRagdollComponent;
class UAnimMontage;
class AEnemyCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDied, AEnemyCharacter*, DeadEnemy);

/**
 * 필드 적 — LLM 없는 일반 게임식 몬스터. 서브퀘스트 토벌 대상(도적·오크·망령).
 * 행동은 AEnemyAIController(배회→추적→공격→복귀), 피격·타격 규약은 ACombatCharacter 그대로라
 * 플레이어 스윙·투사체·아군 NPC 공격이 전부 같은 경로로 맞고, 아군 NPC 시야에도 잡힌다(StimuliSource).
 * SmartNPC 와 달리 NPCManager 에 등록하지 않는다 — 서버 prompt·perception 보고 없음.
 * 사망 시 story_event npc_died(name=EnemyID) → 디렉터 boss_killed 평가(Orc_Vagron 등).
 * 토벌 수량 퀘스트(도적 3명)는 스포너가 킬 수를 세어 flag 로 보낸다(AEnemySpawner::KillFlag).
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API AEnemyCharacter : public ACombatCharacter, public ICharacterBase
{
    GENERATED_BODY()

public:
    AEnemyCharacter();

    /** 종류 식별자(개체 아님) — "Bandit_Raider"/"Orc_Vagron"/"Knight_Wraith".
     *  perception id·story npc_died name·아군 NPC 호감도 키. 같은 종류 전 개체가 하나의 호감도 행을 공유한다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy")
    FString EnemyID = TEXT("Enemy");

    /** 스탯. BeginPlay 에서 BaseStats → 파생치(AttackPower·Defense·MaxHealth·속도) 재계산 후 HP 만땅. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
    FCharacterAttributesBase Attributes;

    /** 공격 몽타주 — `NPC Attack Hit Window` 노티파이 필수(없으면 데미지 0). 미지정 시 타이머 폴백 판정. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat")
    UAnimMontage* AttackMontage = nullptr;

    /** AttackPower → 실데미지 환산 배율. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat")
    float AttackDamageScale = 1.0f;

    /** 공격 시작 후 다음 공격까지 대기(초). 몽타주 길이와 별개. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat")
    float AttackCooldown = 1.5f;

    /** 사망 후 시체 유지(초) — 래그돌 안착·시신 노출 여유. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy")
    float CorpseLifetime = 5.0f;

    /** 사망 시 브로드캐스트 — 스포너가 생존 수·킬 수 집계, BP 는 VFX. */
    UPROPERTY(BlueprintAssignable, Category = "Enemy")
    FOnEnemyDied OnEnemyDied;

    /** 아군 NPC AIPerception 에 보이기 위한 자극원(Sight·Hearing). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
    UAIPerceptionStimuliSourceComponent* StimuliSource;

    /** 액티브 래그돌(Flinch/Knockdown/사망) — SmartNPC 와 같은 컴포넌트. 메시에 Physics Asset 필요. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
    UNPCRagdollComponent* RagdollComponent;

    /** 타겟에게 공격 1회 시작(타겟 저장 + 몽타주). 반환: 이번 공격이 점유하는 시간(초). 이미 공격 중·사망이면 0. */
    float StartAttack(AActor* Target);

    /** 공격 몽타주(또는 폴백 판정 타이머) 진행 중. */
    bool IsAttacking() const;

    /** HP 0 → 사망. 태그·스토리 이벤트·AI 정지·래그돌·수명. 중복 호출 무시. */
    UFUNCTION(BlueprintCallable, Category = "Enemy")
    void HandleDeath();

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        class AController* EventInstigator, AActor* DamageCauser) override;

    virtual FString GetCombatId() const override { return EnemyID; }

    // --- IGameplayTagAssetInterface ---
    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override { TagContainer = GameplayTags; }

    // --- ICharacterBase ---
    virtual FString GetEntityID_Implementation() const override { return EnemyID; }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::NPC; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }
    virtual FCharacterAttributesBase GetAttributes_Implementation() const override { return Attributes; }
    virtual void ApplyResourceDelta_Implementation(float DeltaHealth, float DeltaMana, float DeltaStamina) override
    { Attributes.Resources.ApplyDelta(DeltaHealth, DeltaMana, DeltaStamina); }
    /** 적은 같은 적 종류 외 전부에게 적대. 호감도 조회 없음. */
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const override;

protected:
    virtual void BeginPlay() override;
    virtual float ComputeAttackDamage() const override { return Attributes.Combat.AttackPower * AttackDamageScale; }

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tags", meta = (AllowPrivateAccess = "true"))
    FGameplayTagContainer GameplayTags;

    // 몽타주 미배정 폴백 — 고정 지연 뒤 1회 판정(노티파이 윈도우 대체).
    FTimerHandle FallbackHitTimer;
    void FallbackAttackHit();
};
