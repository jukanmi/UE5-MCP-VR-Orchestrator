#pragma once

#include "CoreMinimal.h"
#include "Core/BP/CombatCharacter.h"
#include "Core/Interfaces/Entity.h"
#include "VillagerCharacter.generated.h"

class UAIPerceptionStimuliSourceComponent;
class UNPCRagdollComponent;
class UNPCDialogueUIComponent;
class UAnimSequence;
class USoundBase;

/** 채팅 키워드 규칙 1개 — 플레이어 문장에 Keywords 중 하나라도 들어 있으면 Lines 에서 랜덤 1줄. */
USTRUCT(BlueprintType)
struct FVillagerKeywordRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    TArray<FString> Keywords;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    TArray<FString> Lines;
};

/**
 * 앰비언트 주민(주민·피난민·학자·상인) — LLM·서버 0 인 마을 배경 NPC.
 * 행동은 AVillagerAIController(아이들↔배회, 근접 인사, 적 시야·피격 시 도주), 피격·사망·래그돌 규약은
 * ACombatCharacter 그대로라 플레이어 스윙·투사체가 같은 경로로 맞고 적 AI 의 IsValidTarget 에도 잡힌다.
 * AEnemyCharacter 와 같은 단일 노드 애니(AnimBP 없음) — 속도로 Idle/Walk/Run, Wave·HitRecieve 는 1회 재생.
 * NPCManager 미등록: 서버 prompt·perception 보고·npc_died·호감도 전부 없음. 반격도 없다.
 * 아군 LLM NPC 의 시야엔 StimuliSource 로 잡히며, 그쪽 perception id 는 GetCombatId()=VillagerID.
 * 대화는 전부 로컬 규칙: Interact → 기본 풀 1줄, 채팅 → 길 안내 키워드(현재 스토리 비트 기준) → 종류별 키워드 규칙 → 기본 풀.
 * 규칙 데이터는 tools/make_villager_bps.py 가 BP CDO 에 굽는다(단일 원본).
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

    /** 머리 위 말풍선 — SmartNPC 와 같은 컴포넌트. 발화자 이름은 ICharacterBase::GetEntityID = VillagerID. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Villager|Components")
    UNPCDialogueUIComponent* DialogueWidgetComp;

    // --- 대화 규칙 (make_villager_bps.py 가 굽는다) ---
    /** Interact 인사·매칭 없는 채팅에 쓰는 기본 대사 풀(랜덤 1줄). 비어 있으면 말풍선 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    TArray<FString> DefaultLines;

    /** 종류별 키워드 규칙 — 앞에서부터 첫 매칭. 길 안내 키워드보다 뒤에 본다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    TArray<FVillagerKeywordRule> KeywordRules;

    /** 길 안내로 분기하는 키워드(전 종류 공통) — "길"·"어디"·"퀘스트"·"가야". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    TArray<FString> DirectionKeywords;

    /** 스토리 비트 id → 목적지 안내문(구역 이름+방향). 전 종류 공통이라 베이스 BP 에 둔다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    TMap<FString, FString> BeatDirections;

    /** 활성 서브퀘스트 id → 덧붙일 한 줄(예: s_moca_herbs "남쪽 숲 빈터 약초"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    TMap<FString, FString> SideDirections;

    /** 스토리 비트 미수신(HasState()==false)·표에 없는 비트일 때 안내문. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Villager|Dialogue")
    FString NoStoryDirection = TEXT("글쎄, 성문 쪽 경비병한테 물어보게.");

    /** 플레이어 Interact — 멈춰 바라보기+Wave(컨트롤러) + 기본 풀 1줄 말풍선. 도주·사망 중이면 false(대사 없음). */
    UFUNCTION(BlueprintCallable, Category = "Villager|Dialogue")
    bool Interact(AActor* Player);

    /** 채팅 로컬 응답 — 길 안내 키워드 → 종류 키워드 규칙 → 기본 풀 순. 말풍선 표시 후 그 문장 반환. 사망이면 빈 문자열. */
    UFUNCTION(BlueprintCallable, Category = "Villager|Dialogue")
    FString RespondToChat(const FString& PlayerText);

    /** 말풍선 1줄. 빈 문자열·사망이면 무시. */
    UFUNCTION(BlueprintCallable, Category = "Villager|Dialogue")
    void Say(const FString& Text);

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

    /** 현재 스토리 비트·활성 서브퀘스트 기준 길 안내 한 문장. */
    FString BuildDirections() const;
    static const FString& PickLine(const TArray<FString>& Pool);
    static bool ContainsAny(const FString& Text, const TArray<FString>& Keywords);

    // 단일 노드 로코모션 — 현재 재생 클립(같은 클립 재요청 방지).
    TWeakObjectPtr<UAnimSequence> CurrentLocoAnim;
    void UpdateLocomotionAnim();
    void PlayLoco(UAnimSequence* Anim);
};
