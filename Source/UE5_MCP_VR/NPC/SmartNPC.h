#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Engine/TimerHandle.h"
#include "../Core/Entity.h"  // INPCEntity → ICharacterEntity → IGameplayTagAssetInterface 포함
#include "NPCStateComponent.h"  // FNPCPlan (OnPlanUpdated 핸들러 시그니처용)

#include "SmartNPC.generated.h"

class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;
class UAIPerceptionComponent;
class UAIPerceptionStimuliSourceComponent;
class UStateTree;
class UBlackboardData;
class UWidgetComponent;
class UNPCAudioStreamComponent;
class UPhysicalAnimationComponent;  // 액티브 래그돌 Flinch 상체 PD
class UAnimMontage;
struct FActionBatch;

// 넉다운 진행 단계. None=평상, Ragdoll=쓰러져 안착 대기, GettingUp=기상 몽타주·블렌드 복귀 중.
enum class EKnockdownPhase : uint8
{
    None,
    Ragdoll,
    GettingUp,
};

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

    // 액티브 래그돌 — Flinch 상체 PD 복귀에 사용. 메시 바인딩은 BeginPlay 에서. PA_SmartNPC 필요.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Ragdoll")
    UPhysicalAnimationComponent* PhysicalAnim;

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

    /** NPC 머리 위 대사 말풍선. WBP 클래스·정밀 위치는 BP 에서 지정. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Dialogue")
    UWidgetComponent* DialogueWidgetComp;

    /** TTS 음성 없이 표시할 때 기본 노출 시간(초). 텍스트 길이에 비례 가산. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitleFallbackDuration = 4.0f;

    /** 글자당 추가 노출 시간(초) — 긴 대사 더 오래. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitlePerCharDuration = 0.05f;

    /** 음성 싱크 모드 안전 상한(초) — Completed 누락(스트림 에러·소켓 끊김) 시 강제 숨김. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitleMaxDuration = 15.0f;

    /** 머리 위 말풍선에 대사 표시. bWaitForAudio=true 면 텍스트만 세팅하고 TTS 음성 시작 시 표시,
     *  false 면 즉시 표시 + 폴백 타이머 후 숨김. 동일 발화 재요청은 깜빡임 없이 이어붙임. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void ShowSubtitle(const FString& Text, bool bWaitForAudio);

    /** 말풍선 숨김 + 텍스트 클리어. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void HideSubtitle();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

    /**
     * [Offline Fallback] NPCManager가 WebSocket 연결 상태 변화 시 호출합니다.
     * BT의 Selector가 IsConnected 키를 감지해 Local Fallback 서브트리로 자동 분기합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SetBlackboardBool(const FString& KeyName, bool bValue);

    // === Death ===

    /** 사망 여부. true이면 NPC맵에서 퇴출 완료 + 모든 액션 중지 상태. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|State")
    bool bIsDead = false;

    /** 사망 시 브로드캐스트. BP에서 사망 애니메이션·VFX 연결용. */
    UPROPERTY(BlueprintAssignable, Category = "MCP|State")
    FOnNPCDied OnNPCDied;

    /** 사망 래그돌에 가할 타격 방향 임펄스 강도(본 단위, bVelChange=false → 질량 의존). 0 이면 순수 중력. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    float DeathImpulseStrength = 20000.0f;

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
    // 액티브 래그돌 — 트리거형 hit-react (§3). 약타=Flinch(상체 PD 복귀), 강타=Knockdown(전신 래그돌→기상).
    // ====================================================================

    /** 전신 패시브 래그돌 진입(사망·넉다운 공유). 캡슐 NoCollision·CMC 정지·메시 시뮬 ON·LastHitDirection 임펄스.
     *  bFatal=true: HandleDeath 경로(임펄스=DeathImpulseStrength). false: 넉다운(×KnockdownImpulseScale). */
    void EnterRagdoll(bool bFatal);

    /** 약타 반응 — 상체(FlinchRootBone 이하) 물리 블렌드 + 임펄스 → Tick 램프로 애니 복귀. 넉다운/기상 중이면 무시. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Ragdoll")
    void Flinch();

    /** 강타 반응 — 전신 래그돌(EnterRagdoll(false)) + AI 정지 + 안착 후 기상. 넉다운/기상 중 재호출 시 재진입(저글). */
    UFUNCTION(BlueprintCallable, Category = "MCP|Ragdoll")
    void Knockdown();

    /** 피격 강도(=½mv² 데미지)로 반응 분기. >= KnockdownImpulseThreshold → Knockdown, else → Flinch.
     *  방향·본은 LastHitDirection/LastHitBone(직전 TakeDamage 가 채움) 사용. */
    void ReactToHit(float HitStrength);

    // --- 분기 임계 ---

    /** 이 데미지(=½mv² 에너지 스케일) 이상이면 넉다운, 미만이면 Flinch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float KnockdownImpulseThreshold = 40.f;

    // --- Flinch (약타) ---

    /** Flinch 물리 블렌드 시작 본(이 본 이하만 시뮬, 하체는 애니 유지). Mixamo=Spine. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    FName FlinchRootBone = TEXT("Spine");

    /** Flinch PD — 애니 포즈로 당기는 방향 강도(클수록 빨리 복귀·뻣뻣). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchOrientationStrength = 1000.f;

    /** Flinch PD — 각속도 감쇠 강도. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchAngularVelStrength = 100.f;

    /** Flinch 피격 임펄스 크기. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchImpulse = 30000.f;

    /** 물리→애니 블렌드 복귀 속도(weight/초). Flinch·기상 블렌드 램프 공용. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float FlinchRecoverSpeed = 3.0f;

    // --- Knockdown / 기상 ---

    /** 넉다운 래그돌 임펄스 배율(DeathImpulseStrength 대비). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float KnockdownImpulseScale = 1.0f;

    /** 안착 판정 본(루트/골반 선속도 측정). Mixamo=Hips. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    FName KnockdownPelvisBone = TEXT("Hips");

    /** 안착 판정 — 골반 선속도가 이 값(cm/s) 미만이어야 안착 카운트. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float SettleSpeedThreshold = 150.f;

    /** 안착 유지 시간(초) — 저속이 이만큼 지속되면 기상 시작. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    float SettleHoldTime = 0.6f;

    /** 누운(등 바닥) 상태 기상 몽타주. 미할당 시 즉시 블렌드 복귀 폴백. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    UAnimMontage* GetUpMontage_FaceUp = nullptr;

    /** 엎드린(얼굴 바닥) 상태 기상 몽타주. 미할당 시 즉시 블렌드 복귀 폴백. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    UAnimMontage* GetUpMontage_FaceDown = nullptr;

    /** 동시 넉다운 상한(멀티 NPC). 초과분은 Flinch 폴백. 트리거형이라 평소 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Ragdoll")
    int32 MaxConcurrentKnockdown = 3;

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

    // --- 액티브 래그돌 내부 상태 ---
    bool bFlinching = false;
    float FlinchBlendWeight = 0.f;          // Flinch 상체 블렌드 추적(읽기 API 없어 자체 보관)
    EKnockdownPhase KnockdownPhase = EKnockdownPhase::None;
    float SettleTimer = 0.f;                // 안착 지속 누적
    float GetUpBlendWeight = 0.f;           // 기상 전신 블렌드 추적
    FName OriginalMeshProfile;              // BeginPlay 캡처 — 기상 후 메시 콜리전 프로파일 복원용
    ECollisionEnabled::Type OriginalMeshCollision = ECollisionEnabled::QueryOnly;  // BeginPlay 캡처 — 활성화 상태 복원용
    FTimerHandle GetUpMontageTimer;

    /** 동시 넉다운 게이트 카운터 소유자(UNPCManager, GameInstanceSubsystem). 없으면 nullptr. */
    class UNPCManager* GetNPCManager() const;

    // --- Tick 헬퍼 ---
    void TickFlinchRamp(float DeltaSeconds);
    void TickSettleDetection(float DeltaSeconds);
    void BeginGetUp();
    void TickGetUpBlend(float DeltaSeconds);
    void FinishGetUp();

    /** 기상 몽타주 종료 델리게이트 — 정상 완료 시 FinishGetUp(인터럽트는 무시). */
    void OnGetUpMontageEnded(class UAnimMontage* Montage, bool bInterrupted);

    /** 모든 Tick 소비자(affinity·자막·flinch·넉다운)를 OR 해 Tick 켜기/끄기 일원화. */
    void RefreshTickEnabled();

    // --- Dialogue Subtitle 내부 ---
    UFUNCTION()
    void HandleSubtitleAudioStarted();
    UFUNCTION()
    void HandleSubtitleAudioCompleted();

    /** NPCAudioStreamComponent 델리게이트 1회 바인딩(재바인딩 누수 방지). */
    void TryBindAudioSubtitle();

    /** 위젯에 현재 텍스트 적용 + 가시성 설정. 표시 중에만 빌보드용 Tick. */
    void ApplySubtitle(bool bVisible);

    // --- Plan 갱신 로그 ---
    /** NPCStateComponent::OnPlanUpdated 구독 핸들러 — plan 갱신 로그 출력. */
    UFUNCTION()
    void HandlePlanUpdated(const FNPCPlan& NewPlan);

    /** OnPlanUpdated 1회 바인딩(재바인딩 누수 방지). */
    bool bPlanUpdatedBound = false;

    // --- 사망 임펄스용 마지막 치명타 정보 (TakeDamage 가 채움, HandleDeath 가 소비) ---
    FName LastHitBone = NAME_None;
    FVector LastHitDirection = FVector::ZeroVector;  // ShotDirection (피격→방향, 정규화)

    // --- 공격 판정 상태 (NPC→타겟) ---
    TWeakObjectPtr<AActor> CurrentAttackTarget;  // ExecuteAttackAction 이 세팅, 노티파이가 소비
    bool bAttackHitConsumed = false;             // 스윙당 1회 가드(NotifyBegin 리셋)

    /** 현재 표시/대기 중 자막 텍스트(중복 발화 억제용). */
    FString CurrentSubtitleText;
    bool bSubtitleWaitingForAudio = false;
    bool bAudioSubtitleBound = false;
    FTimerHandle SubtitleHideTimer;

public:

    /** VR 플레이어 멜리 재타격 쿨다운용 — 마지막 피격 시각(World TimeSeconds). VRPawn 가 읽고 씀.
     *  소유자(NPC) 가 직접 보유 → NPC 소멸 시 함께 사라져 누적/만료정리 불필요. */
    float LastMeleeHitTime = -1000.f;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "MCP|Debug")
    void Debug_PrintAffinity();

    /** 더미 plan 주입 → 머리 위 plan HUD 동작 검증 (파이프라인 없이). */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "MCP|Debug")
    void Debug_TestPlanHUD();

    /** 현재 호감도를 NPC 머리 위에 텍스트로 상시 표시할지 여부. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Debug")
    bool bShowAffinityOnScreen = false;
};
