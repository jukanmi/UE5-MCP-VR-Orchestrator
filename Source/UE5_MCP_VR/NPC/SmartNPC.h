#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "../Core/Entity.h"  // INPCEntity → ICharacterEntity → IGameplayTagAssetInterface 포함

#include "SmartNPC.generated.h"

class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;
class UAIPerceptionComponent;
class UAIPerceptionStimuliSourceComponent;
class UBehaviorTree;
struct FActionBatch;

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




    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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


    UFUNCTION(CallInEditor, Category = "MCP|Debug")
	void Debug_Test_Social_Dialogue();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
	void Debug_Test_Common_Move();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
	void Debug_Test_Combat_Attack();
};
