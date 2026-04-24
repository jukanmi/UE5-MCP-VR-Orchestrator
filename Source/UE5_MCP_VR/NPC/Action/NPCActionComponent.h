#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../../Network/MCPJsonUtils.h" // FGameAction, FActionBatch
#include "../Struct/NPCActionTypes.h" // Enums
#include "../NPCActionDataAsset.h"
#include "EnvironmentQuery/EnvQuery.h"   // EQS 쿼리 에셋 참조용
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "NPCActionComponent.generated.h"

// --- EQS+LLM 전술 위치 결정 파이프라인 상태 ---
// WHY: BTTask가 async 패턴(InProgress → Tick → Succeeded)으로 폴링하기 위한 상태 머신.
UENUM()
enum class ETacticalQueryState : uint8
{
    Idle,          // 쿼리 없음 (기본)
    WaitingEQS,    // EQS AllMatching 쿼리 실행 중
    WaitingLLM,    // EQS 완료, LLM 응답 대기 중
    ResultReady,   // LLM 응답 수신, 결과 준비 완료
    Failed,        // 실패 (EQS 없음 / LLM 오류 등)
};

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionStateChanged, const FGameAction&, Action);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllActionsStopped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNPCDialogue, const FString&, AgentID, const FString&, DialogueText);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCActionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPCActionComponent();

    // --- Action Events ---
    // Blackboard 제어 결합도를 낮추기 위한 이벤트 (SmartNPCAIController 등이 바인딩하여 사용)
    UPROPERTY(BlueprintAssignable, Category = "NPC|Action|Events")
    FOnActionStateChanged OnActionStarted;

    UPROPERTY(BlueprintAssignable, Category = "NPC|Action|Events")
    FOnAllActionsStopped OnActionStoppedAll;

    UPROPERTY(BlueprintAssignable, Category = "NPC|Events|Dialogue")
    FOnNPCDialogue OnNPCDialogue;

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Debug")
    bool bEQSDebugDraw = false;

    // 후보 구체 표시 지속 시간 (초)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Debug", meta = (EditCondition = "bEQSDebugDraw", ClampMin = "1.0", ClampMax = "30.0"))
    float EQSDebugDuration = 8.f;

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

    /** EQS+LLM 분업 파이프라인용: AllMatching 모드로 다수 후보 위치를 뽑는 쿼리.
     *  WHY: 기존 SingleResult 쿼리는 EQS만으로 최선 위치를 고르지만,
     *       여기서는 LLM이 최종 선택하도록 복수 후보가 필요하다.
     *  미할당 시 DefaultMoveQuery로 폴백. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* TacticalPositionsQuery;     // 전술 위치 후보 다중 쿼리

    // --- 전술 위치 결정 파이프라인 상태 (BTTask가 폴링) ---

    ETacticalQueryState TacticalQueryState = ETacticalQueryState::Idle;

    /** 최종 선택된 전술 이동 목적지 (LLM이 chosen_id로 선택한 후보의 위치) */
    FVector TacticalQueryResult = FVector::ZeroVector;

    /** LLM이 응답한 chosen_id로 위치를 역조회하기 위한 맵 */
    TMap<FString, FVector> TacticalCandidateMap;

    // --- Action Queue State ---
    TQueue<FGameAction> ActionQueue;
    
    // 현재 진행 중인 액션 캐싱 (BTTask 등에서 참조)
    FGameAction CurrentAction;

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    const FGameAction& GetCurrentAction() const { return CurrentAction; }

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsBusy = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsDialogueActive = false;

    // --- Public API: Batch & Queue ---

    // [의도(Why)] LLM으로부터 수신된 다중 행동(ActionBatch)을 순차 처리하기 위해 큐 트랜잭션을 시작하고 주요 상태를 갱신합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Action")
    void ExecuteActionBatch(const FActionBatch& Batch);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    bool ProcessNextAction();

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

    // --- 전술 위치 파이프라인 내부 ---
    TArray<FVector> CachedEnemyLocations; // StartTacticalQuery → OnTacticalCandidatesDone 전달용

    /** EQS AllMatching 콜백: 후보 스코어링 + LLM 전송 */
    void OnTacticalCandidatesDone(TSharedPtr<struct FEnvQueryResult> Result);

    // 후보 위치 시각화 (Pruned 목록 기준)
    void DrawEQSCandidates(const TArray<FLocationCandidate>& Candidates, float Duration = 5.f) const;
    // 최종 선택 위치 시각화 (LLM reason 포함)
    void DrawEQSChosenLocation(const FVector& Loc, const FString& CandidateId, const FString& Reason = TEXT(""), float Duration = 8.f) const;

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

    // EQS 실행 완료 시 호출되는 콜백 (SingleResult - 기존 ExecuteMove 용)
    void OnTacticalMoveCompleted(TSharedPtr<struct FEnvQueryResult> Result);

    // ============================================================================
    // [전술 위치 결정 파이프라인 API]
    // ============================================================================

    /** BTTask_PrepareNextAction이 호출 → EQS(AllMatching) 실행 → 스코어링 → LLM 전송.
     *  @param EnemyLocations  현재 인지된 적 위치 목록 (스코어링에 사용)
     *  완료 시 TacticalQueryState = ResultReady, TacticalQueryResult에 위치 저장. */
    void StartTacticalQuery(const TArray<FVector>& EnemyLocations);

    /** NPCManager가 LLM 응답 수신 시 호출.
     *  ChosenCandidateId → TacticalCandidateMap 역조회 → ResultReady 상태로 전환. */
    void NotifyLocationDecisionReady(const FString& ChosenCandidateId, const FString& Reason = TEXT(""));

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
