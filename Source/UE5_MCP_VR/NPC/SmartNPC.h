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

private:
    void DestroyAfterDeath();

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

    /** 현재 표시/대기 중 자막 텍스트(중복 발화 억제용). */
    FString CurrentSubtitleText;
    bool bSubtitleWaitingForAudio = false;
    bool bAudioSubtitleBound = false;
    FTimerHandle SubtitleHideTimer;

public:

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "MCP|Debug")
    void Debug_PrintAffinity();

    /** 더미 plan 주입 → 머리 위 plan HUD 동작 검증 (파이프라인 없이). */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "MCP|Debug")
    void Debug_TestPlanHUD();

    /** 현재 호감도를 NPC 머리 위에 텍스트로 상시 표시할지 여부. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Debug")
    bool bShowAffinityOnScreen = false;
};
