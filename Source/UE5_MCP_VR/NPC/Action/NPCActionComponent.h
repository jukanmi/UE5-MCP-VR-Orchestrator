#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../../Network/MCPJsonUtils.h" // FGameAction, FActionBatch
#include "../Struct/NPCActionTypes.h" // Enums
#include "../NPCActionDataAsset.h"
#include "EnvironmentQuery/EnvQuery.h"   // EQS 쿼리 에셋 참조용
#include "NPCActionComponent.generated.h"

// --- 전술적 이동 상태 Enum ---
// LLM이 전송하는 "TacticalState" JSON 값과 1:1 대응합니다.
UENUM(BlueprintType)
enum class ETacticalMoveState : uint8
{
    Default     UMETA(DisplayName = "Default"),   // 기본 이동 (EQS 미사용 또는 기본 쿼리)
    Cover       UMETA(DisplayName = "Cover"),     // 엄폐·은신 위치 탐색
    Flanking    UMETA(DisplayName = "Flanking"),  // 적 측면 포위
    Retreat     UMETA(DisplayName = "Retreat"),   // 후방 안전지대 후퇴
    HighGround  UMETA(DisplayName = "HighGround"),// 고지대 우선 선점
    Ambush      UMETA(DisplayName = "Ambush"),    // 잠복 대기 지점
};

class ASmartNPCAIController;
class UNPCActionDataAsset;
class UNPCStateComponent;
class UNPCInventoryComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCActionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPCActionComponent();

    // --- Dependencies (외부 컴포넌트 참조) ---
    // Owner에서 자동 검색하므로 Blueprint에서 직접 설정할 필요 없음
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Action|Refs")
    UNPCStateComponent* StateComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Action|Refs")
    UNPCInventoryComponent* InventoryComponent;

    // --- Action Data Assets ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|Interaction")
    UNPCActionDataAsset* ActionData;

    // --- Tactical EQS Query Assets ---
    // 언리얼 에디터에서 상황별 EQS 에셋을 할당하세요.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* DefaultMoveQuery;    // 기본 이동 쿼리

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* CoverFinderQuery;    // 은폐 위치 탐색 쿼리

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* FlankingQuery;       // 측면 포위 탐색 쿼리

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* RetreatQuery;        // 후방 안전지대 탐색 쿼리

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* RangedOptimalPositionQuery; // 원거리 최적 포지션 쿼리

    // --- Action Queue State ---
    TQueue<FGameAction> ActionQueue;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsBusy = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsDialogueActive = false;

    // --- Public API: Batch & Queue ---

    // [의도(Why)] LLM으로부터 수신된 다중 행동(ActionBatch)을 순차 처리하기 위해 큐 트랜잭션을 시작하고 주요 상태를 갱신합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Action")
    void ExecuteActionBatch(const FActionBatch& Batch);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    void ProcessNextAction();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    void OnActionCompleted();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    void StopAllActions();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action")
    void AbortCurrentAction();

protected:
    virtual void BeginPlay() override;

    // --- Internal Helpers ---
    void UpdateActionState(const FGameAction& Action);
    void DispatchActions(const TArray<FGameAction>& Actions);

    // Movement Speed 변환 (EMoveType → float)
    float ParseMoveSpeed(const EMoveType& Type) const;

    // ============================================================================
    // [기본 함수 (Base Functions)] 래퍼함수 구현시 사용하는 유틸 함수
    // ============================================================================
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseMove(FVector TargetLocation, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseDialogue(const FString& DialogueText, const EFacialState Emotion);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseEmotion(const EFacialState Emotion);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseFaceRotate(FVector TargetLocation, float TurnSpeed = 5.f);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSitDown(AActor* TargetSeat);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSitUp();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseLieDown(AActor* TargetBed);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseLieUp();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSignalAllies(const FString& SignAssetID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseComfort(AActor* TargetActor);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseEmote(const FString& EmoteAssetID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseDance(const FString& DanceAssetID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSing(const FString& SingAssetID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseStopCurrentAction();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    TMap<FString, int32> BaseDetectEntityInRange(float Range, EEntityType EntityType);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSendEventToActor(AActor* TargetActor, const FString& EventName);


    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BasePlayActionMedia(const FString& AssetID);

    // [의도(Why)] 전술 이동(EQS) 시작 직전에 변동된 스탯을 파라미터에 미리 주입하여 가장 합리적인 위치를 도출하게 합니다.
    void UpdateEQSParams();

private:
    // Cached references
    ASmartNPCAIController* GetOwnerAIController() const;
    FString GetOwnerAgentID() const;
public:
    // ============================================================================
    // [EAction 래퍼 함수 (Action Wrappers)]
    // ============================================================================

    // [의도(Why)] LLM에서 들어오는 위치 데이터 형식("(X=...,Y=...)")을 엔진 좌표계 객체(FVector)로 안전하게 변환합니다.
    FVector ParseVectorParam(const FString& ParamStr) const;

    // [의도(Why)] JSON으로 언패킹된 파라미터들을 각 세부 액션(Execute~)의 인자로 알맞게 매핑 및 라우팅합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteInteraction(EAction ActionType, AActor* TargetActor, const TMap<FString, FString>& Params);

    // ----------------------------------------------------------------------------
    // [1] Common Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteIdle();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteMove(FVector Location, AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk,
                     ETacticalMoveState TacticalState = ETacticalMoveState::Default);

    // EQS 실행 완료 시 호출되는 콜백
    void OnTacticalMoveCompleted(TSharedPtr<struct FEnvQueryResult> Result);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteFollow(AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDialogue(const FString& DialogueText, const EFacialState Emotion);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteTurnTo(FVector Location, AActor* TargetActor); 

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteScan(FVector Location, AActor* TargetActor);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteUseItem(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteEquipAction(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteUnequipAction(const FString& ItemID);

    // ----------------------------------------------------------------------------
    // [2] Combat Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteAttackAction(AActor* TargetActor, EAttackType AttackType);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteBlock(AActor* TargetActor);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDodgeAction(FVector Direction);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteFlee(FVector EscapeLocation);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSignalAllies(const FString& HandSign);

    // ----------------------------------------------------------------------------
    // [3] Social Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, int32 GiveAmount, const FString& GetItemID, int32 GetAmount);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteGiveItem(AActor* TargetActor, const FString& ItemID, int32 Amount);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteComfort(AActor* TargetActor);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteHandObject(const FString& ItemID);

    // ----------------------------------------------------------------------------
    // [4] Task Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecutePickUp(FVector Location);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDrop(const FString& ItemID);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteCraft(const TArray<FString>& ItemIDs);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteRepair(const FString& ItemID);

    // ----------------------------------------------------------------------------
    // [5] Investigation Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteInvestigate(FVector Location);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteScout(FVector StartLocation, FVector EndLocation);

    // ----------------------------------------------------------------------------
    // [6] Lifestyle Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteLifestyleAction(EAction LifestyleType, AActor* TargetEntity, FVector Location, const FString& StringParam);

};
