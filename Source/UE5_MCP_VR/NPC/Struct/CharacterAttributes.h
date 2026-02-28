#pragma once

#include "CoreMinimal.h"
#include "CharacterAttributes.generated.h"

/**
 * TRPG-style Base Stats
 * These are core attributes that affect derived stats and skill checks.
 * Range: 1-100 (default 10)
 */
USTRUCT(BlueprintType)
struct FBaseStats
{
    GENERATED_BODY()

    // Physical Attributes
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Physical", meta = (ClampMin = "1", ClampMax = "999"))
    int32 Strength = 10;  // 물리 공격력, 운반 능력

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Physical", meta = (ClampMin = "1", ClampMax = "999"))
    int32 Dexterity = 10;  // 회피, 명중률, 이동속도

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Physical", meta = (ClampMin = "1", ClampMax = "999"))
    int32 Agility = 10;  // 공격속도, 크리티컬 확률

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Physical", meta = (ClampMin = "1", ClampMax = "999"))
    int32 Constitution = 10;  // 체력, 지구력, 독/질병 저항

    // Mental Attributes
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Mental", meta = (ClampMin = "1", ClampMax = "999"))
    int32 Intelligence = 10;  // 마법 공격력, 학습 속도

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Mental", meta = (ClampMin = "1", ClampMax = "999"))
    int32 Wisdom = 10;  // 마법 방어력, 마나 회복

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Mental", meta = (ClampMin = "1", ClampMax = "999"))
    int32 Perception = 10;  // 감지, 함정 발견, 은신 탐지

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Stats|Luck", meta = (ClampMin = "1", ClampMax = "100"))
    int32 Luck = 10;  // 드롭률, 크리티컬, 랜덤 이벤트

    FBaseStats() {}
};


/**
 * Game Resources
 * Dynamic values that change during gameplay.
 */
USTRUCT(BlueprintType)
struct FGameResources
{
    GENERATED_BODY()

    // Vital Resources
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Vital")
    float Health = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Vital")
    float MaxHealth = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Vital")
    float Mana = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Vital")
    float MaxMana = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Vital")
    float Stamina = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Vital")
    float MaxStamina = 100.0f;

    // Regeneration Rates (per second)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Regen")
    float HealthRegen = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Regen")
    float ManaRegen = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resources|Regen")
    float StaminaRegen = 10.0f;

    FGameResources() {}

    // Helper: Is Alive?
    bool IsAlive() const { return Health > 0.0f; }

    // Helper: Resource Percentages
    float GetHealthPercent() const { return MaxHealth > 0 ? Health / MaxHealth : 0.0f; }
    float GetManaPercent() const { return MaxMana > 0 ? Mana / MaxMana : 0.0f; }
    float GetStaminaPercent() const { return MaxStamina > 0 ? Stamina / MaxStamina : 0.0f; }
};


/**
 * Combat Stats
 * Derived from Base Stats, affected by equipment/buffs.
 */
USTRUCT(BlueprintType)
struct FCombatStats
{
    GENERATED_BODY()

    // Offensive
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Offense")
    float AttackPower = 10.0f;  // 물리 공격력

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Offense")
    float MagicPower = 10.0f;  // 마법 공격력

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Offense")
    float AttackSpeed = 1.0f;  // 공격 속도 배율

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Offense")
    float CriticalChance = 5.0f;  // 크리티컬 확률 (%)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Offense")
    float CriticalMultiplier = 150.0f;  // 크리티컬 배율 (%)

    // Defensive
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Defense")
    float Defense = 10.0f;  // 물리 방어력

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Defense")
    float MagicResist = 10.0f;  // 마법 저항력

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Defense")
    float DodgeChance = 5.0f;  // 회피율 (%)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Defense")
    float BlockChance = 0.0f;  // 막기 확률 (%)

    // Accuracy
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Accuracy")
    float Accuracy = 90.0f;  // 명중률 (%)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Accuracy")
    float Range = 200.0f;  // 기본 공격 사거리 (UU)

    FCombatStats() {}
};


/**
 * Behavioral Traits (행동 특성)
 * Used by AI for decision-making. Range: 0-100.
 * Matches Python BehavioralTraits in behavior_policy.py
 */
USTRUCT(BlueprintType)
struct FBehavioralTraits
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavioral", meta = (ClampMin = "0", ClampMax = "100"))
    float Aggression = 50.0f;  // 공격 성향

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavioral", meta = (ClampMin = "0", ClampMax = "100"))
    float Fear = 50.0f;  // 공포/도주 성향

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavioral", meta = (ClampMin = "0", ClampMax = "100"))
    float Bravery = 50.0f;  // 위험 감수 성향

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavioral", meta = (ClampMin = "0", ClampMax = "100"))
    float Sanity = 100.0f;  // 정신력 (낮으면 판단 노이즈 증가)

    FBehavioralTraits() {}
};


/**
 * Movement Stats (이동 스탯)
 */
USTRUCT(BlueprintType)
struct FMovementStats
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float WalkSpeed = 200.0f;  // 걷기 속도 (UU/s)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float RunSpeed = 400.0f;  // 달리기 속도 (UU/s)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float SprintSpeed = 600.0f;  // 전력질주 속도 (UU/s)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float CrouchSpeed = 100.0f;  // 웅크리기 속도 (UU/s)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float JumpHeight = 300.0f;  // 점프 높이 (UU)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float SwimSpeed = 150.0f;  // 수영 속도 (UU/s)

    FMovementStats() {}
};


/**
 * Status Effects (상태 이상)
 * Bitflags for active debuffs/buffs.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EStatusEffect : uint8
{
    None        = 0         UMETA(Hidden),
    Poisoned    = 1 << 0,   // 독: 지속 피해
    Burning     = 1 << 1,   // 화상: 지속 피해 + 방어력 감소
    Frozen      = 1 << 2,   // 빙결: 이동속도 감소
    Stunned     = 1 << 3,   // 기절: 행동 불가
    Silenced    = 1 << 4,   // 침묵: 마법 사용 불가
    Bleeding    = 1 << 5,   // 출혈: 지속 피해
    Feared      = 1 << 6,   // 공포: 도주
    Confused    = 1 << 7,   // 혼란: 랜덤 행동
};
ENUM_CLASS_FLAGS(EStatusEffect);


/**
 * Complete Character Attributes
 * Aggregates all stat categories.
 */
USTRUCT(BlueprintType)
struct FCharacterAttributes
{
    GENERATED_BODY()

    // Base TRPG Stats (능력치)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FBaseStats BaseStats;

    // Dynamic Resources (자원)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FGameResources Resources;

    // Derived Combat Stats (전투)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FCombatStats Combat;

    // AI Behavioral Traits (행동)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FBehavioralTraits Behavior;

    // Movement (이동)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FMovementStats Movement;

    // Active Status Effects (상태이상)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes", meta = (Bitmask, BitmaskEnum = "EStatusEffect"))
    uint8 StatusEffects = 0;

    // Level & Experience
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes|Progression")
    int32 Level = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes|Progression")
    int32 Experience = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes|Progression")
    int32 ExperienceToNextLevel = 100;

    FCharacterAttributes() {}

    // Helper: Apply Status Effect
    void AddStatusEffect(EStatusEffect Effect)
    {
        StatusEffects |= static_cast<uint8>(Effect);
    }

    void RemoveStatusEffect(EStatusEffect Effect)
    {
        StatusEffects &= ~static_cast<uint8>(Effect);
    }

    bool HasStatusEffect(EStatusEffect Effect) const
    {
        return (StatusEffects & static_cast<uint8>(Effect)) != 0;
    }

    // Helper: Recalculate Combat Stats from Base Stats
    void RecalculateCombatStats()
    {
        Combat.AttackPower = BaseStats.Strength * 1.5f;
        Combat.MagicPower = BaseStats.Intelligence * 1.5f;
        Combat.Defense = BaseStats.Constitution * 1.0f;
        Combat.MagicResist = BaseStats.Wisdom * 1.0f;
        Combat.CriticalChance = BaseStats.Agility * 0.5f + BaseStats.Luck * 0.3f;
        Combat.DodgeChance = BaseStats.Dexterity * 0.5f;
        Combat.Accuracy = 70.0f + BaseStats.Dexterity * 0.3f;

        Resources.MaxHealth = 50.0f + BaseStats.Constitution * 5.0f;
        Resources.MaxMana = 20.0f + BaseStats.Intelligence * 3.0f + BaseStats.Wisdom * 2.0f;
        Resources.MaxStamina = 50.0f + BaseStats.Constitution * 2.0f + BaseStats.Dexterity * 2.0f;

        // Movement Speed Calculation with Asymptotic Scaling
        // Properties:
        //   - Dex ≤ 10 (BaseDex) → Speed = BaseSpeed (기본값 유지)
        //   - Dex → ∞           → Speed → MaxSpeed (600)
        constexpr float MaxSpeed = 600.0f;
        constexpr float BaseDex = 10.0f;
        constexpr float K = 50.0f;  // 곡선 가파름 조절
        
        // Base speeds at Dex = 10
        constexpr float BaseWalk = 200.0f;
        constexpr float BaseRun = 400.0f;
        constexpr float BaseSprint = 500.0f;
        
        // Calculate asymptotic factor: 0 at Dex≤10, approaches 1 as Dex→∞
        const float DexDiff = FMath::Max(0.0f, static_cast<float>(BaseStats.Dexterity) - BaseDex);
        const float AsymptoticFactor = DexDiff / (DexDiff + K);
        
        Movement.WalkSpeed = BaseWalk + (MaxSpeed - BaseWalk) * AsymptoticFactor;
        Movement.RunSpeed = BaseRun + (MaxSpeed - BaseRun) * AsymptoticFactor;
        Movement.SprintSpeed = BaseSprint + (MaxSpeed - BaseSprint) * AsymptoticFactor;
    }
};


/**
 * Player-specific Character Attributes
 * Same as FCharacterAttributes but WITHOUT FBehavioralTraits (AI-only).
 * Use this for player characters.
 */
USTRUCT(BlueprintType)
struct FPlayerAttributes
{
    GENERATED_BODY()

    // Base TRPG Stats (능력치)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FBaseStats BaseStats;

    // Dynamic Resources (자원)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FGameResources Resources;

    // Derived Combat Stats (전투)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FCombatStats Combat;

    // Movement (이동)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes")
    FMovementStats Movement;

    // Active Status Effects (상태이상)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes", meta = (Bitmask, BitmaskEnum = "EStatusEffect"))
    uint8 StatusEffects = 0;

    // Level & Experience
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes|Progression")
    int32 Level = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes|Progression")
    int32 Experience = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes|Progression")
    int32 ExperienceToNextLevel = 100;

    FPlayerAttributes() {}

    // Helper: Apply Status Effect
    void AddStatusEffect(EStatusEffect Effect)
    {
        StatusEffects |= static_cast<uint8>(Effect);
    }

    void RemoveStatusEffect(EStatusEffect Effect)
    {
        StatusEffects &= ~static_cast<uint8>(Effect);
    }

    bool HasStatusEffect(EStatusEffect Effect) const
    {
        return (StatusEffects & static_cast<uint8>(Effect)) != 0;
    }

    // Helper: Recalculate Combat Stats from Base Stats (same as FCharacterAttributes)
    void RecalculateCombatStats()
    {
        Combat.AttackPower = BaseStats.Strength * 1.5f;
        Combat.MagicPower = BaseStats.Intelligence * 1.5f;
        Combat.Defense = BaseStats.Constitution * 1.0f;
        Combat.MagicResist = BaseStats.Wisdom * 1.0f;
        Combat.CriticalChance = BaseStats.Agility * 0.5f + BaseStats.Luck * 0.3f;
        Combat.DodgeChance = BaseStats.Dexterity * 0.5f;
        Combat.Accuracy = 70.0f + BaseStats.Dexterity * 0.3f;

        Resources.MaxHealth = 50.0f + BaseStats.Constitution * 5.0f;
        Resources.MaxMana = 20.0f + BaseStats.Intelligence * 3.0f + BaseStats.Wisdom * 2.0f;
        Resources.MaxStamina = 50.0f + BaseStats.Constitution * 2.0f + BaseStats.Dexterity * 2.0f;

        // Movement Speed Calculation with Asymptotic Scaling
        constexpr float MaxSpeed = 600.0f;
        constexpr float BaseDex = 10.0f;
        constexpr float K = 50.0f;
        
        constexpr float BaseWalk = 200.0f;
        constexpr float BaseRun = 400.0f;
        constexpr float BaseSprint = 500.0f;
        
        const float DexDiff = FMath::Max(0.0f, static_cast<float>(BaseStats.Dexterity) - BaseDex);
        const float AsymptoticFactor = DexDiff / (DexDiff + K);
        
        Movement.WalkSpeed = BaseWalk + (MaxSpeed - BaseWalk) * AsymptoticFactor;
        Movement.RunSpeed = BaseRun + (MaxSpeed - BaseRun) * AsymptoticFactor;
        Movement.SprintSpeed = BaseSprint + (MaxSpeed - BaseSprint) * AsymptoticFactor;
    }
};
