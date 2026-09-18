#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatCharacter.generated.h"

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
 * 전투 캐릭터 공통 베이스 — SmartNPC(LLM NPC)·EnemyCharacter(필드 적)가 공유하는 피격/타격 규약.
 * 플레이어 근접 스윙·투사체(KineticDamage)·공격 노티파이·래그돌·아군 AI 의 사망 판정은 이 타입만 안다.
 * 여기 없는 것(LLM 인지·호감도·스탯 컴포넌트)은 각 파생 클래스 몫.
 */
UCLASS(Abstract)
class UE5_MCP_VR_API ACombatCharacter : public ACharacter
{
    GENERATED_BODY()

public:
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

protected:
    /** 이번 스윙의 원시 데미지(방어력 차감 전). 파생 클래스가 자기 스탯에서 산출. */
    virtual float ComputeAttackDamage() const { return 0.f; }

private:
    TWeakObjectPtr<AActor> CurrentAttackTarget;  // 공격 시작 시 세팅, 노티파이가 소비
    bool bAttackHitConsumed = false;             // 스윙당 1회 가드(NotifyBegin 리셋)
};
