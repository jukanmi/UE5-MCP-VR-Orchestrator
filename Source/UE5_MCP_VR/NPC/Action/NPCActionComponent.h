#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../../Network/MCPJsonUtils.h" // FGameAction, FActionBatch
#include "../Struct/NPCActionTypes.h" // Enums
#include "../NPCActionDataAsset.h"
#include "EnvironmentQuery/EnvQuery.h"   // EQS 쿼리 에셋 참조용
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "NPCActionComponent.generated.h"

// --- EQS+LLM 전술 위치 결정 파이프라인 상태 ---
// WHY: STTask_PrepareNextAction이 Tick에서 폴링하기 위한 상태 머신.
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

    // --- EQS 에셋 (2개로 한정) ---
    // 이동 경로는 두 가지 정책으로 분리됩니다:
    //   [A] ExecuteMove → DefaultMoveQuery (SingleResult) : LLM이 지시한 목적지로 이동
    //   [B] TryStartTacticalQueryForCombat → TacticalPositionsQuery (AllMatching + LLM) : Perception 트리거 전술 재배치

    // [A] LLM 지시 이동용 (SingleResult). 미할당 시 BaseMove 직접 호출로 폴백.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* DefaultMoveQuery;

    // [B] Perception 트리거 전술 재배치용 (AllMatching). 미할당 시 DefaultMoveQuery로 폴백.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* TacticalPositionsQuery;

    // --- 전술 위치 결정 파이프라인 상태 (STTask가 폴링) ---

    ETacticalQueryState TacticalQueryState = ETacticalQueryState::Idle;

    /** 최종 선택된 전술 이동 목적지 (LLM이 chosen_id로 선택한 후보의 위치) */
    FVector TacticalQueryResult = FVector::ZeroVector;

    /** LLM이 응답한 chosen_id로 위치를 역조회하기 위한 맵 */
    TMap<FString, FVector> TacticalCandidateMap;

    /** EQS 요청 세대 카운터 — Start/Abort 마다 증가. 응답이 일치할 때만 처리해 stale 응답 차단.
     *  (ID 가 SAFE/OPTIMAL/AGGRESSIVE 고정이라 신/구 응답 구분이 안 되는 문제 해결) */
    uint32 TacticalQueryGeneration = 0;

    // --- Action Queue State ---
    TQueue<FGameAction> ActionQueue;

    // 마지막으로 큐에 들어간 액션 타입 — 동일 타입 연속 중복 추가 방지용
    EAction LastQueuedActionType = EAction::Idle;

    // 현재 진행 중인 액션 캐싱 (STTask 등에서 참조)
    FGameAction CurrentAction;

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    const FGameAction& GetCurrentAction() const { return CurrentAction; }

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    ENPCBehaviorMode CurrentBehaviorMode = ENPCBehaviorMode::Common;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsBusy = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsDialogueActive = false;

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    bool HasPendingActions() const { return !ActionQueue.IsEmpty(); }

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

    // EQS 전술 가중치 묶음. UpdateEQSParams / StartTacticalQuery 양쪽이 공유.
    struct FEQSWeights
    {
        float SearchRadius = 1000.f;
        float CoverWeight = 0.f;
        float DistanceWeight = 0.f;
        float AggressionWeight = 0.f;
        float SafeDistance = 0.f;
    };
    FEQSWeights ComputeEQSWeights() const;

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
    const FString& GetOwnerAgentID() const { return CachedAgentID; }

    /** BeginPlay에서 한 번 캐시한 AgentID. 매 호출마다 reflection 조회를 피하기 위함. */
    FString CachedAgentID;

    // --- Track 상태 ---
    /** Track 중인 대상. 유효하지 않으면 트래킹 중단. */
    TWeakObjectPtr<AActor> TrackedTarget;

    /** 주기적 위치 갱신 타이머 핸들 */
    FTimerHandle TrackTimer;

    /** TrackTimer 콜백: 대상이 유효하면 MoveToActor 재발행, 아니면 타이머 정지. */
    void UpdateTrackPosition();
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

    /** Perception 이벤트에서 호출 → EQS(AllMatching) 실행 → 스코어링 → LLM 전송.
     *  @param EnemyLocations  현재 인지된 적 위치 목록 (스코어링에 사용)
     *  완료 시 TacticalQueryState = ResultReady, TacticalQueryResult에 위치 저장. */
    void StartTacticalQuery(const TArray<FVector>& EnemyLocations);

    /** Perception 이벤트에서 호출 — 쿨다운 & 상태 체크 후 전술 쿼리 시작.
     *  결과 Move 액션은 ActionQueue에 자동 enqueue되어 BT가 자연스럽게 처리. */
    void TryStartTacticalQueryForCombat(const TArray<FVector>& EnemyLocations);

    /** LLM 응답 파싱 실패 시 NPCManager가 호출 — WaitingLLM 상태를 Idle로 복구해 BT hang 방지. */
    void AbortTacticalQuery();

    /** 전술 쿼리 재발동 최소 간격 (초). 연속 SIGHT/HEARING에 매번 쿼리하지 않도록 방지. */
    /** EQS 재요청 쿨다운(초). Start 또는 Abort 시점부터 카운트.
     *  주의: gemma e4b cold-start 가 ~6초이므로 그 이하로 줄이면 응답 도착 전에
     *  새 hearing 트리거가 곧바로 EQS 를 재시작 → 매번 stale 로 처리되는 루프 발생. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|EQS", meta = (ClampMin = "0.5", ClampMax = "30.0"))
    float TacticalQueryCooldown = 6.0f;

    // === EQS / 전술 스코어링 튜닝 파라미터 ===
    // UpdateEQSParams() 및 EvalSafe/Aggressive/OptimalScore()에서 사용

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float EQS_SearchRadiusBase = 1000.f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float EQS_PerceptionRadiusScale = 20.f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_SafeDistScale = 3.f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_CoverBonus = 2.f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_LOSPenalty = 1.f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_LowHpFleeBonus = 1.5f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_AggrDistScale = 3.f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_AggrLOSBonus = 2.f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_AggrCoverPenalty = 0.5f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_AggrHpBonus = 1.f;

    // --- Optimal 스코어 튜닝 ---
    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_OptIdealDist = 800.f;   // 이 거리가 최고점

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_OptDistRange = 800.f;   // IdealDist ± Range 를 벗어나면 0점

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_OptCoverBonus = 2.5f;

    UPROPERTY(EditAnywhere, Category = "MCP|Tuning")
    float Score_OptLOSBonus = 2.f;

    /** LLM 응답 대기 최대 시간 (초). 초과 시 AbortTacticalQuery 자동 호출.
     *  주의: gemma e4b thinking 모델은 num_predict=300 시 5~15초 걸림.
     *  너무 짧게 잡으면 매번 Abort 되어 결과가 영구히 적용 안 됨. */
    UPROPERTY(EditAnywhere, Category = "NPC|Action|EQS", meta = (ClampMin = "2.0", ClampMax = "60.0"))
    float TacticalLLMTimeout = 20.0f;

    FTimerHandle TacticalLLMTimeoutTimer;

    /** 마지막 전술 쿼리 시작 시각 (TimeSeconds). 쿨다운 체크용. */
    float LastTacticalQueryTime = -1000.0f;

    // 이동 완료 후 재생할 몽타주 키 (Attack 등 근접 도착 후 재생)
    FString PendingMoveMediaKey;

    void OnAttackMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);

    /** NPCManager가 LLM 응답 수신 시 호출.
     *  ChosenCandidateId → TacticalCandidateMap 역조회 → ResultReady 상태로 전환.
     *  RequestGen 이 현재 TacticalQueryGeneration 과 다르면 stale 응답으로 간주하고 무시. */
    void NotifyLocationDecisionReady(const FString& ChosenCandidateId, const FString& Reason = TEXT(""), uint32 RequestGen = 0);

    /** 현재 EQS 요청 세대 번호 (Python 으로 보내고 그대로 echo 받음) */
    uint32 GetTacticalQueryGeneration() const { return TacticalQueryGeneration; }

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

    /** 대상 액터를 지속 추적. 방해 없으면 계속 따라다님.
     *  TrackTimer(0.5s 주기)로 MoveToActor를 갱신하며 OnActionCompleted/StopAllActions 시 자동 해제. */
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteTrack(AActor* TargetActor);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteScout(FVector StartLocation, FVector EndLocation);

    // ----------------------------------------------------------------------------
    // [6] Lifestyle Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteLifestyleAction(EAction LifestyleType, AActor* TargetEntity, FVector Location, const FString& StringParam);

};
