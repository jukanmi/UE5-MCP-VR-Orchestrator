#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BehaviorTree/BehaviorTree.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "../Network/MCPJsonUtils.h"
#include "Struct/NPCActionTypes.h"
#include "../Core/GameStateData.h"
#include "Struct/CharacterAttributes.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "SmartNPC.generated.h"

class UNPCActionDataAsset;
class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;


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
class UE5_MCP_VR_API ASmartNPC : public ACharacter
{
    GENERATED_BODY()

public:
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
    
    /** NPC 자신이 시각/청각 인식 대상으로 등록되기 위한 컴포넌트 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|AI|Perception")
    UAIPerceptionStimuliSourceComponent* StimuliSource;

    // 플리커링 및 위치 기반 청각 유추를 위한 이전 타겟 기록 (GC Safe)
    TMap<TWeakObjectPtr<AActor>, FKnownTargetInfo> KnownTargetsMap;

    // 대상을 정확히 식별할 수 있는 기본 최대 거리 (이 거리 밖이면 "unknown" 처리)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Identity")
    float BaseIdentificationRadius = 1500.f;

    // 사운드 발생 시, 기존에 기억해둔 타겟 위치와 얼마나 가까워야 동일 인물로 볼 것인가
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Identity")
    float HearingAssociationRadius = 300.f; // 3미터 이내

    // 기억 유효 시간 (시야에서 사라진 지 몇 초까지 소리를 연동할 것인가)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception|Identity")
    float TargetMemoryTTL = 5.0f; // 5초

    /** 
     * TODO: 순수 시각/청각 인지(Perception) 결과를 실시간으로 담아두는 버퍼 변수를 이 위치에 추가할 예정입니다. 
     */

public:

    // === Lifecycle ===

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



    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void OnActionCompleted();


    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Social_Dialogue();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Common_Move();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Combat_Attack();
};
