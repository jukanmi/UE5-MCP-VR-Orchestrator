#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/NPCEntity.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "Perception/AIPerceptionTypes.h"
#include "SmartNPC.generated.h"

class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;
class UAIPerceptionComponent;
class UAIPerceptionStimuliSourceComponent;

USTRUCT(BlueprintType)
struct FKnownTargetInfo
{
	GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Memory")
    FVector LastLocation = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Memory")
    float LastSeenTime = 0.f; 
};

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API ASmartNPC : public ACharacter, public IGameplayTagAssetInterface, public INPCEntity
{
	GENERATED_BODY()

public:
    // --- IGameplayTagAssetInterface 구현 ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tags")
    FGameplayTagContainer GameplayTags;

    UFUNCTION(BlueprintCallable, Category = "Tags")
    void AddStateTag(FGameplayTag Tag);

    UFUNCTION(BlueprintCallable, Category = "Tags")
    void RemoveStateTag(FGameplayTag Tag);

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

    /** 이 NPC가 사용할 Behavior Tree */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    UBehaviorTree* BehaviorTreeAsset;

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

    TMap<TWeakObjectPtr<AActor>, FKnownTargetInfo> KnownTargetsMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Identity")
    float BaseIdentificationRadius = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Identity")
    float HearingAssociationRadius = 300.f; // 3미터 이내

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Identity")
    float TargetMemoryTTL = 5.0f; // 5초

    /** 
     * TODO: 순수 시각/청각 인지(Perception) 결과를 실시간으로 담아두는 버퍼 변수를 이 위치에 추가할 예정입니다. 
     */

public:


    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // === Facade API (컴포넌트로 위임) ===

    /** 액션 배치 실행 → ActionComponent에 위임 */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    virtual void ExecuteActionBatch(const struct FActionBatch& Batch);

    /**
     * [Time-Slicing 콜백] NPCManager의 주기 타이머가 호출합니다.
     * NPC는 자신의 현재 FGameStateData를 채워서 SendStateToMCP를 통해 Python에 전송합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    virtual void CollectAndSendStateUpdate();

    /**
     * [Offline Fallback] NPCManager가 WebSocket 연결 상태 변화 시 호출합니다.
     * BT의 Selector가 IsConnected 키를 감지해 Local Fallback 서브트리로 자동 분기합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SetBlackboardBool(const FString& KeyName, bool bValue);

    /** 물리 상태 초기화 (애니메이션 중지, 이동 정지)
     *  NOTE: PolicyCacheComponent 삭제 후 현재 미호출.
     *        향후 Emergency Cognition / Offline Fallback 로직에서 재활용 예정. */
    virtual void ClearPhysicalState();

    // === Damage Hook (UE5 Actor Override) ===

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        class AController* EventInstigator, AActor* DamageCauser) override;

    // === Convenience Accessors (자주 호출되는 것들의 Shortcut) ===

    /** Stats에 직접 접근하기 위한 편의 함수 */
    FCharacterAttributes GetStats() const;



    // === IEntity / INPCEntity 인터페이스 구현 ===

    virtual FString GetEntityID_Implementation() const override { return AgentID; }
    virtual EEntityType GetEntityType_Implementation() const override { return EEntityType::NPC; }
    virtual FVector GetEntityLocation_Implementation() const override { return GetActorLocation(); }
    virtual float GetHealth_Implementation() const override;
    virtual bool IsAlive_Implementation() const override;
    virtual bool IsHostileTo_Implementation(const TScriptInterface<ICharacterEntity>& Other) const override { return false; } // TODO: 팩션 시스템 연동
    virtual FString GetAgentID_Implementation() const override { return AgentID; }
    virtual void ExecuteActionBatch_Implementation(const FActionBatch& Batch) override;

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void OnActionCompleted();


    UFUNCTION(CallInEditor, Category = "MCP|Debug")
	void Debug_Test_Social_Dialogue();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
	void Debug_Test_Common_Move();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
	void Debug_Test_Combat_Attack();
};
