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
// WHY: 진행 중 여부로 신규 요청을 게이트하기 위한 상태 머신 (폴링 아님 — 결과는 ActionQueue 주입).
// 터미널 상태(Failed/ResultReady)를 두지 않는다: != Idle 재시작 가드에 걸려 영구 고착되므로
// 실패·완료 모두 즉시 Idle 복귀, 재시도 억제는 LastTacticalQueryTime 쿨다운이 담당.
UENUM()
enum class ETacticalQueryState : uint8
{
    Idle,          // 쿼리 없음 (기본)
    WaitingEQS,    // EQS AllMatching 쿼리 실행 중
    WaitingLLM,    // EQS 완료, LLM 응답 대기 중
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
class AAIController;
class AFurnitureActor;
class UNPCActionDataAsset;
class UNPCStateComponent;
class UNPCInventoryComponent;
class UInventoryComponent;
class UAnimMontage;

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
    // 전술 재배치는 단일 경로로 통합됨:
    //   TryStartTacticalQueryForCombat / ExecuteMove(목적지 미지정) → StartTacticalQuery
    //     → TacticalPositionsQuery (AllMatching) + Python location_decision
    //   ExecuteMove(목적지 지정) → BaseMove 직접 호출 (EQS 미사용)

    // TacticalPositionsQuery 미할당 시 AllMatching 폴백으로 사용 (StartTacticalQuery, .cpp 참조).
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* DefaultMoveQuery;

    // [B] Perception 트리거 전술 재배치용 (AllMatching). 미할당 시 DefaultMoveQuery로 폴백.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|EQS")
    UEnvQuery* TacticalPositionsQuery;

    // --- 전술 위치 결정 파이프라인 상태 (진행 중 게이트용 — 결과는 ActionQueue 주입) ---

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

    // BehaviorMode 소유는 NPCStateComponent. 여기선 read 위임만 제공(STTask 등 호출 편의).
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    ENPCBehaviorMode GetBehaviorMode() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsBusy = false;

    /** 현재 액션이 비동기 완료(이동 도착/몽타주 종료)를 기다리는 중인지 여부.
     *  ExecuteInteraction이 매 진입 시 false로 리셋하고, BaseMove/BasePlayActionMedia가 콜백을
     *  걸면 true로 설정. switch 종료 후 false면 즉시 OnActionCompleted를 호출(즉시형 액션). */
    bool bActionAwaitingAsync = false;

    /** 비동기 완료 신호가 끝내 오지 않는 액션(도달 불가 MoveTo, 몽타주 누락 등) 대비 워치독.
     *  ExecuteInteraction 진입 시 타이머 시작, OnActionCompleted/Abort 시 해제.
     *  초과하면 강제로 OnActionCompleted를 호출해 큐가 영구 정지하는 것을 막는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue", meta = (ClampMin = "1.0", ClampMax = "120.0"))
    float MaxActionDuration = 15.f;

    FTimerHandle ActionWatchdogTimer;

    /** Flee 패닉(얼어붙기) 지연 타이머 — 액션 중단 시 ClearActiveActionState 가 취소.
     *  로컬 핸들로 두면 중단 후에도 발화해 stale Flee 가 실행됨. */
    FTimerHandle FleePanicTimer;

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

    /** 진행 중 액션의 공통 런타임 상태 리셋(bIsBusy/비동기대기/PendingMedia/워치독).
     *  OnActionCompleted/AbortCurrentAction/StopAllActions가 공유. 태그 revert·큐 비우기 등
     *  각 함수 고유 로직은 호출부에 둔다. */
    void ClearActiveActionState();

    /** 자세 플래그(bIsSit/bIsLie) 해제 — 중단(AbortCurrentAction)·전면 정지(StopAllActions) 전용.
     *  ClearActiveActionState 에 넣으면 안 된다: OnActionCompleted 도 그걸 호출하므로 앉기
     *  몽타주가 끝나는 즉시 자세가 풀린다. 자세는 액션 실행 플래그가 아니라 지속 상태다. */
    void ResetPostureFlags();

    /** 점유 가구 반납(Release + Reset). Sit/Sleep(지속 상태) 만 가구 점유 — ResetPostureFlags 경유.
     *  Read/Pray 는 가구 없는 제자리 액션이라 점유·반납 없음. */
    void ReleaseOccupiedFurniture();

    // Movement Speed 변환 (EMoveType → float)
    float ParseMoveSpeed(const EMoveType& Type) const;

    // Parameters["style"] 문자열 → EMoveType. 미매칭·빈 문자열은 Walk 폴백.
    EMoveType ParseMoveStyle(const FString& StyleStr) const;

    // ============================================================================
    // [기본 함수 (Base Functions)] 래퍼함수 구현시 사용하는 유틸 함수
    // ============================================================================
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseMove(FVector TargetLocation, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

    /** BaseMove 의 액터 추적판 — MoveToActor 로 움직이는 타겟을 따라가고(자동 재경로),
     *  AcceptanceRadius 이내 도달 시 OnMoveActionCompleted 발화. 완료·몽타주 체인은 BaseMove 와 동일. */
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseMoveToActor(AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

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


    /** 몽타주를 재생했으면 true 반환(완료는 몽타주 종료 콜백이 처리). 재생할 몽타주가 없으면 false(즉시형). */
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    bool BasePlayActionMedia(const FString& AssetID);

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

    // --- 전투 셀렉터 연속성 상태 (리셋은 ResetCombatSelectorState 일괄 — 개별 리셋 금지) ---
    /** 직전 셀렉터 선택 — 연속 동일행동 페널티·Attack 상한 판정용. */
    EAction LastCombatChoice = EAction::Idle;

    /** 동일 선택 연속 횟수. */
    int32 ConsecutiveCombatChoiceCount = 0;

    /** 마지막 셀렉터 발동 시각(TimeSeconds) — CombatActionInterval 페이싱용. */
    float LastCombatSelectTime = -1000.f;

    /** 이번 전투에서 SignalAllies 를 이미 발동했는지 — 반복 신호 방지. */
    bool bSignaledAlliesThisCombat = false;

    /** SignalAlliesRadius 내 생존·비적대 NPC 존재 여부(자신·적 제외) — SignalAllies 후보 편입 게이트. */
    bool HasNearbyAlly(const AActor* EnemyTarget) const;
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

    // ============================================================================
    // [전술 위치 결정 파이프라인 API]
    // ============================================================================

    /** Perception 이벤트에서 호출 → EQS(AllMatching) 실행 → 스코어링 → LLM 전송.
     *  @param EnemyLocations  현재 인지된 적 위치 목록 (스코어링에 사용)
     *  완료 시 NotifyLocationDecisionReady가 Move 액션을 ActionQueue에 주입 후 Idle 복귀. */
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

    // 이동 완료 후 재생할 몽타주 키 (Attack 등 근접 도착 후 재생). 비어있으면 도착 즉시 완료.
    FString PendingMoveMediaKey;

    /** 이동 중인 Sit/Sleep 의 가구 목적지 — 도착 시 PlayPendingMoveMedia 가 스냅·점유에 소비.
     *  이동 중단 시 ClearActiveActionState 가 리셋(점유 전이므로 Release 불필요). */
    TWeakObjectPtr<AFurnitureActor> PendingFurnitureTarget;

    /** 현재 점유 중인 가구 — 해제는 ResetPostureFlags 단일 경로(bIsSit/bIsLie 와 동일 라이프사이클). */
    TWeakObjectPtr<AFurnitureActor> OccupiedFurniture;

    /** BaseMove의 MoveTo 완료 콜백(OnRequestFinished 바인딩).
     *  PendingMoveMediaKey가 있으면 도착 후 몽타주 재생(완료는 몽타주 종료가 처리),
     *  없으면 즉시 OnActionCompleted. 도착 실패 시에도 OnActionCompleted로 큐를 푼다. */
    void OnMoveActionCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);

    /** MoveTo 즉시 결과(AlreadyAtGoal/Failed) 동기 처리 — OnRequestFinished 미발화 케이스.
     *  방치 시 bActionAwaitingAsync 잔존으로 워치독까지 정지. BaseMove/BaseMoveToActor 공용. */
    void HandleImmediateMoveResult(AAIController* AIController, EPathFollowingRequestResult::Type MoveResult);

    /** 액션 미디어 재생 + 자세 플래그(bIsSit/bIsLie) — 몽타주가 실제 재생된 경우에만 자세를
     *  세운다(미디어 미등록 시 '앉은 상태인데 서 있는' 불일치 방지). 반환: 재생 여부.
     *  이동 후 재생(도착·AlreadyAtGoal)과 제자리 재생(BaseSitDown/BaseLieDown) 공용. */
    bool PlayActionMediaWithPosture(const FString& MediaKey);

    /** BasePlayActionMedia가 건 몽타주 종료 콜백(Montage_SetEndDelegate). OnActionCompleted 호출. */
    void OnMontageActionEnded(UAnimMontage* Montage, bool bInterrupted);

    /** Dodge 등속 이동 — 몽타주 재생 성공 시 마찰·제동 0 후 RunSpeed×배율로 Launch(고정 방향 감쇠 없이 유지).
     *  원복(StopDodgeMove)은 ClearActiveActionState 단일 경로 — 정상 종료·중단·워치독 전부 커버. */
    void StartDodgeMove(const FVector& Direction);
    void StopDodgeMove();
    bool bDodgeMoveActive = false;
    float SavedGroundFriction = 8.f;
    float SavedBrakingDecelWalking = 2048.f;
    float SavedBrakingFrictionFactor = 2.f;

    /** MaxActionDuration 초과 시 강제 완료(워치독). */
    void HandleActionWatchdog();

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

    // ============================================================================
    // [전투 행동 셀렉터 (Combat Action Selector)] — SPEC_combat_selector Phase 1
    // 큐가 빈 Combat 상태에서 STTask_PrepareNextAction 이 호출하는 C++ 척수 반사층.
    // LLM 재상담 없음 — 가중치 확률 + DiceSystem 주사위로 다음 전투 행동을 주입한다.
    // 성격 차별화는 스탯 파생(Strength→공격, Agility→회피/기동, Fear·Bravery→도주)
    // + 아래 전역 배율 튜닝만 — NPC별 에디터 수작업 없음.
    // ============================================================================

    /** Combat 중 다음 행동을 선택해 ActionQueue 에 주입. 페이싱 간격 미충족·후보 전멸 시 false.
     *  후보: Attack / Dodge / Block / 거리조절(Move) / Flee / SignalAllies — 전부 기존 실행·완료 경로 재사용. */
    bool SelectCombatAction(AActor* TargetActor);

    /** 셀렉터 연속성 상태 리셋. StopAllActions 및 비전투 배치 수신 시 자동 호출. */
    void ResetCombatSelectorState();

    /** 셀렉터 최소 발동 간격(초) — 연속 주입 사이 숨고르기(연속 공격 상한과 별개 페이싱). */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float CombatActionInterval = 0.6f;

    /** 연속 동일 행동 1회당 가중치 배율(횟수만큼 거듭제곱 누적). */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CombatRepeatPenalty = 0.5f;

    /** Attack 연속 상한 — 도달 시 다음 선택에서 Attack 가중치 0(다른 행동 강제). */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "1", ClampMax = "10"))
    int32 MaxConsecutiveAttacks = 3;

    // --- 행동별 기본 가중치(스탯·상황 배율의 기준점) ---
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector")
    float CombatWeight_Attack = 1.0f;

    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector")
    float CombatWeight_Dodge = 0.5f;

    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector")
    float CombatWeight_Block = 0.4f;

    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector")
    float CombatWeight_Spacing = 0.35f;

    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector")
    float CombatWeight_Flee = 0.4f;

    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector")
    float CombatWeight_Signal = 0.5f;

    /** 스탯 정규화 기준 — 가중치 배율 = 스탯/이 값 (10 = 평균 스탯이 배율 1.0). */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "1.0"))
    float CombatStatNorm = 10.f;

    /** 최근 피격 판정 윈도우(초, LastHitTime 기준) — 이내면 방어 행동 부스트. */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "0.0"))
    float RecentHitWindow = 2.0f;

    /** 최근 피격 시 Dodge/Block 가중치 배율. */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "1.0"))
    float RecentHitDefenseBoost = 2.0f;

    /** 저HP 방어 가중치 스케일 — Dodge/Block ×(1 + scale×(1-HP비율)). 하드 임계 없음(스펙). */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "0.0"))
    float LowHPDefenseScale = 1.5f;

    /** 저HP 도주 가중치 스케일 — Flee = 기본 × scale × (1-HP비율)² × 겁 성향. 만HP≈0. */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "0.0"))
    float LowHPFleeScale = 4.0f;

    /** Dodge/Block 이 유의미한 근접 거리(cm) — 밖이면 가중치 ×0.1. */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "100.0"))
    float DefenseReactRange = 700.f;

    /** 거리조절 발동 링(cm) — Min 미만=백스텝 욕구, Max 초과=접근 욕구, 목적지는 Ideal 링. */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "50.0"))
    float SpacingMinRange = 250.f;

    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "100.0"))
    float SpacingMaxRange = 900.f;

    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "100.0"))
    float SpacingIdealRange = 500.f;

    /** Flee 선택 시 배짱 주사위 난이도 — CheckReflex(Bravery, 이 값) 성공하면 도주 취소 후 재선택.
     *  1 = Bravery% 확률로 버팀(용감한 놈 끝까지, 겁쟁이 일찍 도망). */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "1", ClampMax = "10"))
    int32 FleeBraveryDifficulty = 1;

    /** SignalAllies 아군 탐색 반경(cm). 발동은 전투당 1회. */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "100.0"))
    float SignalAlliesRadius = 2000.f;

    /** Dodge 등속 이동 속도 = RunSpeed × 이 배율 — 항상 달리기보다 빠름 보장.
     *  Dodge 몽타주 재생 동안 고정 방향 유지(StartDodgeMove), 종료·중단 시 원복(StopDodgeMove). */
    UPROPERTY(EditAnywhere, Category = "MCP|CombatSelector", meta = (ClampMin = "1.0", ClampMax = "5.0"))
    float DodgeSpeedMultiplier = 1.5f;

    // ============================================================================
    // 척수반사 테이블 (SPEC_reflex_table)
    // ----------------------------------------------------------------------------
    // WHY: 접적 반응을 Python SLM 에 물어보면 WS 왕복 + debounce + 추론으로 수백 ms~수 초가
    //      걸리고, 서버가 죽으면 반사 자체가 사라진다. 규칙이 이미 결정론이므로 C++ 에서 바로
    //      실행한다. 매칭·추첨·주입이 전부 이 컴포넌트 안에 있는 이유는 ActionQueue 가 여기
    //      private 이고, 기존 주입 경로(전투 셀렉터·EQS 결과)도 전부 컴포넌트 내부라서다.
    //      컨트롤러는 자극을 넘기는 TryReflexReact 호출 하나만 한다.
    // ============================================================================

    /** 퍼셉션 자극 하나를 반사 테이블에 걸어보고, 맞으면 액션을 큐에 주입한다.
     *
     *  @param Sense        자극 감각(Sight/Hearing)
     *  @param EventType    Hearing 소음 태그("Drop" 등). Sight 면 빈 문자열.
     *  @param SourceID     자극 발생 대상의 이름 — 관계 판정·Attack 타겟에 쓰인다.
     *  @param BaseDanger   호감도 배율을 곱하기 **전**의 원본 위험도.
     *  @param Distance     대상까지 거리(cm)
     *  @param StimulusLoc  자극 위치 — 소음 조사·바라보기 목적지.
     *  @return 반사가 실제로 발동했으면 true.
     *
     *  진행 중 액션은 강탈하지 않는다(bIsBusy·큐 비어있음 요구) — 전투 셀렉터와 같은 규율. */
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Reflex")
    bool TryReflexReact(ESenseType Sense, const FString& EventType, const FString& SourceID,
                        float BaseDanger, float Distance, const FVector& StimulusLoc);

    /** 반사 룰 테이블. 기본값은 생성자에서 확정(바이너리에만 두지 말 것).
     *  위에서부터 검사해 **처음 맞는 룰 하나만** 발동하므로, 좁은 조건을 위에 둘 것. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Reflex")
    TArray<FReflexRule> ReflexRules;

    /** 룰과 무관한 NPC 단위 최소 간격(초). 서로 다른 룰이 번갈아 튀는 것을 막는다. */
    UPROPERTY(EditAnywhere, Category = "MCP|Reflex", meta = (ClampMin = "0.0", ClampMax = "30.0"))
    float ReflexGlobalCooldown = 1.5f;

private:
    /** 룰별 마지막 발동 시각. ReflexRules 와 인덱스 정합(첫 호출 시 크기 맞춤). */
    TArray<float> ReflexRuleLastFireTime;

    /** NPC 단위 마지막 반사 시각. */
    float LastReflexTime = -1000.f;

    /** 룰 하나가 이 자극에 걸리는지 판정. */
    bool DoesReflexRuleMatch(const FReflexRule& Rule, ESenseType Sense, const FString& EventType,
                             ENPCRelation Relation, float BaseDanger, float Distance) const;

    /** 가중 분포 추첨. 후보가 없으면 EAction::Idle 반환. */
    static EAction PickWeightedReflexAction(const TMap<EAction, float>& Weights);

public:

    // ----------------------------------------------------------------------------
    // [3] Social Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, int32 GiveAmount, const FString& GetItemID, int32 GetAmount);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteGiveItem(AActor* TargetActor, const FString& ItemID, int32 Amount);

    /** GiveItem 수령처 해석 — TargetActor 의 UInventoryComponent 우선, 없으면 플레이어 폰 폴백.
     *  NPC↔NPC 전달도 같은 경로를 탄다. */
    UInventoryComponent* ResolveReceiverInventory(AActor* TargetActor) const;


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

    /** 자세 해제(앉기/눕기 → 기립). 현재 자세 플래그가 몽타주를 결정하며, 서 있으면 무동작. */
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteStandUp();

};
