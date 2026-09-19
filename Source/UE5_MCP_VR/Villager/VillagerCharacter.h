#pragma once

#include "CoreMinimal.h"
#include "Core/BP/CombatCharacter.h"
#include "Core/Interfaces/Entity.h"
#include "VillagerCharacter.generated.h"

class UAIPerceptionStimuliSourceComponent;
class UNPCRagdollComponent;
class UAnimSequence;
class USoundBase;

/**
 * 앰비언트 주민(주민·피난민·학자·상인) — LLM·서버 0 인 마을 배경 NPC.
 * 행동은 AVillagerAIController(아이들↔배회, 근접 인사, 적 시야·피격 시 도주), 피격·사망·래그돌 규약은
 * ACombatCharacter 그대로라 플레이어 스윙·투사체가 같은 경로로 맞고 적 AI 의 IsValidTarget 에도 잡힌다.
 * AEnemyCharacter 와 같은 단일 노드 애니(AnimBP 없음) — 속도로 Idle/Walk/Run, Wave·HitRecieve 는 1회 재생.
 * NPCManager 미등록: 서버 prompt·perception 보고·npc_died·호감도 전부 없음. 반격도 없다.
 * 아군 LLM NPC 의 시야엔 StimuliSource 로 잡히며, 그쪽 perception id 는 GetCombatId()=VillagerID.
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API AVillagerCharacter : public ACombatCharacter, public ICharacterBase
{
    GENERATED_BODY()

public:
    AVillagerCharacter();

    /** 개체 식별자 — "Townsfolk_3"/"Refugee_1". 말풍선 발화자·디버그·아군 NPC perception id. AgentID 아님(서버 키 아님). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager")
    FString VillagerID = TEXT("Villager");

    /** 스탯. BeginPlay 에서 BaseStats → 파생치(Defense·MaxHealth·속도) 재계산 후 HP 만땅. 공격은 안 한다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Stats")
    FCharacterAttributesBase Attributes;

    /** 홈(배치 지점) 주변 배회 반경(cm). 0 이면 제자리 아이들만. 인스턴스마다 다르게 두려고 컨트롤러가 아닌 폰이 가진다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager")
    float WanderRadius = 600.f;

    // --- 애니메이션 (단일 노드) — 미지정 슬롯은 건너뜀(T 포즈). ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Anim")
    UAnimSequence* IdleAnim = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Anim")
    UAnimSequence* WalkAnim = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Anim")
    UAnimSequence* RunAnim = nullptr;

    /** 인사(손 흔들기) 1회. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Anim")
    UAnimSequence* WaveAnim = nullptr;

    /** 비치사 피격 1회. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Anim")
    UAnimSequence* HitAnim = nullptr;

    // --- 사운드 — 배열이면 매번 랜덤 1개. 비어 있으면 무음. ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Sound")
    TArray<USoundBase*> HitSounds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Sound")
    TArray<USoundBase*> DeathSounds;

    /** 사망 후 시체 유지(초). 리스폰 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager")
    float CorpseLifetime = 10.f;

    /** 아군 NPC AIPerception 에 보이기 위한 자극원(Sight·Hearing). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Villager|Components")
    UAIPerceptionStimuliSourceComponent* StimuliSource;

    /** 액티브 래그돌(Flinch/Knockdown/사망) — SmartNPC·적과 같은 컴포넌트. 메시에 Physics Asset 필요. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Villager|Components")
    UNPCRagdollComponent* RagdollComponent;

    /** 손 흔들기 1회 재생. 반환: 클립 길이(초). 원샷 재생 중·사망·넘어짐이면 0. */
    float PlayWave();

    /** Wave/Hit 원샷 클립 진행 중 — 컨트롤러는 이 동안 이동·재판단을 쉰다. */
    bool IsPlayingOneShot() const;

    /** HP 0 → 사망. 태그·AI 정지·래그돌·수명. 중복 호출 무시. 서버엔 아무것도 안 보낸다. */
    UFUNCTION(BlueprintCallable, Category = "Villager")
    void HandleDeath();

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        class AController* EventInstigator, AActor* DamageCauser) override;

    virtual FString GetCombatId() const override { return VillagerID; }

    // --- IGameplayTagAssetInterface ---
    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override { TagContainer = GameplayTags; }

    // --- ICharacterBase ---
    virtual FString GetEntityID_Implementation() const override { return VillagerID; }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::NPC; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }
    virtual FCharacterAttributesBase GetAttributes_Implementation() const override { return Attributes; }
    virtual void ApplyResourceDelta_Implementation(float DeltaHealth, float DeltaMana, float DeltaStamina) override
    { Attributes.Resources.ApplyDelta(DeltaHealth, DeltaMana, DeltaStamina); }
    /** 주민은 누구에게도 적대하지 않는다. */
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const override { return false; }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tags", meta = (AllowPrivateAccess = "true"))
    FGameplayTagContainer GameplayTags;

    // 원샷(Wave/Hit) 종료 시각 — 끝나면 로코모션 틱이 Idle/Walk/Run 으로 되돌린다.
    float OneShotEndTime = -1.f;
    float PlayOneShot(UAnimSequence* Anim);

    void PlayOneOf(const TArray<USoundBase*>& Sounds) const;

    // 단일 노드 로코모션 — 현재 재생 클립(같은 클립 재요청 방지).
    TWeakObjectPtr<UAnimSequence> CurrentLocoAnim;
    void UpdateLocomotionAnim();
    void PlayLoco(UAnimSequence* Anim);
};
