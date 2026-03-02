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

class UNPCInteractionDataAsset;
class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;


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

    // === EQS 비동기 캐싱 시스템 ===

    /** 에디터에서 할당할 EQS 에셋 (예: EQS_FindCover) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|EQS")
    class UEnvQuery* TacticalCoverQuery;

    /** 비동기 EQS 결과를 저장하는 캐시 버퍼 (CollectAndSendStateUpdate가 읽기만 함) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|AI|EQS")
    TArray<FEQSResult> CachedEQSResults;

protected:
    /** EQS 갱신 타이머 핸들 (난수 분산 적용) */
    FTimerHandle EQSRefreshTimerHandle;

    /** 비동기 EQS 쿼리를 실행하는 내부 함수 */
    void RefreshTacticalEQS();

    /** 비동기 EQS 쿼리 완료 시 호출되는 콜백. 
     * [참고] TSharedPtr은 리플렉션 시스템(UFUNCTION)에서 지원하지 않으므로 매크로를 제거합니다. 
     * FQueryFinishedSignature는 일반 델리게이트이므로 UFUNCTION 없이도 바인딩 가능합니다. */
    void OnCoverQueryFinished(TSharedPtr<FEnvQueryResult> Result);

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

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Orchestra_Pipeline();
};
