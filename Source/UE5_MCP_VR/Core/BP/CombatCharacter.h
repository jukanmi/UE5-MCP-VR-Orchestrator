#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagContainer.h"
#include "CombatCharacter.generated.h"

class UNPCRagdollComponent;
class UAIPerceptionStimuliSourceComponent;
class USoundBase;
struct FCharacterAttributesBase;

// 히트스캔이 채워주는 본 이름을 부위로 분류해 부위별 데미지 배율을 적용하기 위함.
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

/**
 * 전투 캐릭터 공통 베이스 — SmartNPC(LLM NPC)·EnemyCharacter(필드 적)·VillagerCharacter(주민)가 공유하는 피격/타격 규약.
 * 플레이어 근접 스윙·투사체(KineticDamage)·공격 노티파이·래그돌·아군 AI 의 사망 판정은 이 타입만 안다.
 * 공통으로 갖는 것: 래그돌·지각 자극원 컴포넌트, 상태 태그, AI 회전 설정, 피격 부위 배율·사망 시작 처리.
 * 여기 없는 것(LLM 인지·호감도·스탯·애니메이션 방식)은 각 파생 클래스 몫.
 */
UCLASS(Abstract)
class UE5_MCP_VR_API ACombatCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ACombatCharacter();

    /** 액티브 래그돌(Flinch/Knockdown/사망) — 메시에 Physics Asset 필요. 서브오브젝트 이름 "Ragdoll" 에 NPC BP 들의 튜닝값이 묶여 있다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UNPCRagdollComponent* RagdollComponent;

    /** 아군 NPC AIPerception 에 보이기 위한 자극원(Sight·Hearing). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UAIPerceptionStimuliSourceComponent* StimuliSource;

    /** 상태 태그(State.*). 파생 클래스의 GetOwnedGameplayTags(IGameplayTagAssetInterface)가 이걸 돌려준다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tags")
    FGameplayTagContainer GameplayTags;

    void AddStateTag(FGameplayTag Tag);
    void RemoveStateTag(FGameplayTag Tag);

    /** 사망 여부. true 면 피격·타격 전부 무시(Destroy 지연 동안에도 즉시 사망 판정). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|State")
    bool bIsDead = false;

    /** perception·호감도·승리 보고·스토리 boss_id 에 쓰는 안정 식별자.
     *  객체 이름(BP_…_C_UAID_…)은 레벨 재배치·스폰마다 바뀌므로 파생 클래스가 고정 ID 를 돌려준다. */
    virtual FString GetCombatId() const { return GetName(); }

    /** 임의 액터 사망 판정 — 전투 캐릭터는 bIsDead, 그 외(플레이어 폰)는 State.Condition.Dead 태그. */
    static bool IsActorDead(const AActor* Actor);

    /** 본 이름 → 부위 배율(머리 2.0 / 몸통 1.0 / 사지 0.75). 본 미식별(None·캡슐 히트)은 몸통. */
    static float BodyPartMultiplierForBone(FName Bone);

    // ====================================================================
    // 공격 판정 (자신→타겟) — 공격 몽타주의 AnimNotifyState_NPCAttackHit 가 구동.
    // 지정된 단일 타겟만 거리·arc 게이트로 확인 후 스윙당 1회 데미지(친선사격 없음).
    // ====================================================================

    /** 공격 시작 전 타겟 저장. 노티파이 윈도우가 이 단일 타겟만 타격. */
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

    /** 플레이어 피격 시 넉백 속도(cm/s, LaunchCharacter XY). 0=넉백 끔. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    float NPCKnockbackSpeed = 400.f;

    /** 공격 판정 디버그 — 타겟·거리·명중을 화면/로그에 표시. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Combat")
    bool bDebugAttackHit = false;

    /** VR 플레이어 멜리 재타격 쿨다운용 — 마지막 피격 시각(World TimeSeconds). VRPawn 가 읽고 씀.
     *  피격자가 직접 보유 → 소멸 시 함께 사라져 누적/만료정리 불필요. */
    float LastMeleeHitTime = -1000.f;

    /** 뼈 선분 + 반지름 몸 모양(플레이어 손 충돌 판정용). 뼈대 계열별 에셋, 이 메시의 치수 행이 있어야 쓰인다.
     *  쓰이면 몸 캡슐은 손 채널을 무시하고 손은 뼈 캡슐에만 막힌다. `npc.DrawBoneCapsules 1` 로 그려 볼 수 있다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MCP|Collision")
    TObjectPtr<class UNPCBoneCapsuleSet> BoneCapsules;

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

    /** 이번 스윙의 원시 데미지(방어력 차감 전). 파생 클래스가 자기 스탯에서 산출. */
    virtual float ComputeAttackDamage() const { return 0.f; }

    /** 사망 처리 시작 — 이미 죽었으면 false. 사망 플래그를 세우고 상태 태그를 전부 비운 뒤 State.Condition.Dead 를 단다. */
    bool BeginDeath();

    /** 피격 공통 — 맞은 본으로 부위 배율을 정하고 래그돌에 피격 본·방향을 남긴다(사망·넉다운·Flinch 가 소비).
     *  PointDamage 가 아니면(폭발·일반 데미지) 이전 피격 방향이 남지 않게 가해자 → 나 방향, 배율 1. @return 부위 배율 */
    float NoteHit(FDamageEvent const& DamageEvent, const AActor* DamageCauser);

    /** 소리 배열에서 랜덤 1개 재생(같은 소리 반복 회피). 비어 있으면 무음. */
    void PlayOneOf(const TArray<USoundBase*>& Sounds) const;

    /** BP 에서 편집한 BaseStats → 파생치(공격력·방어력·최대 HP·속도) 재계산, HP 는 만땅, 걷기 속도 적용. */
    void InitAttributes(FCharacterAttributesBase& Attributes);

private:
    TWeakObjectPtr<AActor> CurrentAttackTarget;  // 공격 시작 시 세팅, 노티파이가 소비
    bool bAttackHitConsumed = false;             // 스윙당 1회 가드(NotifyBegin 리셋)
};
