/**
 * SmartNPC.cpp - Lightweight Facade Implementation
 * 
 * [역할] 초기화, 등록/해제, TakeDamage 후킹, Debug 함수만 담당.
 * 모든 행동/상태 로직은 NPCActionComponent와 NPCStateComponent에 위임.
 */

#include "SmartNPC.h"
#include "NPCManager.h"
#include "Action/SmartNPCAIController.h"
#include "Struct/NPCActionKeys.h"
#include "NPCStateComponent.h"
#include "Action/NPCActionComponent.h"
#include "NPCInventoryComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "BehaviorTree/BlackboardComponent.h"

ASmartNPC::ASmartNPC()
{
    PrimaryActorTick.bCanEverTick = true;
    AgentID = TEXT("UnknownAgent");
    AIControllerClass = ASmartNPCAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    // 컴포넌트 생성: 각각 독립적으로 동작하며 BeginPlay에서 상호 참조 설정
    StateComponent     = CreateDefaultSubobject<UNPCStateComponent>(TEXT("StateComponent"));
    ActionComponent    = CreateDefaultSubobject<UNPCActionComponent>(TEXT("ActionComponent"));
    InventoryComponent = CreateDefaultSubobject<UNPCInventoryComponent>(TEXT("InventoryComponent"));
}

void ASmartNPC::BeginPlay()
{
    Super::BeginPlay();

    // NPCManager에 자신을 등록 (Python 서버에서 AgentID로 라우팅)
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->RegisterNPC(AgentID, this);
        }
    }

    // Note: StateComponent->BeginPlay()에서 RefreshStats()를 자동 호출합니다.

    // === EQS 비동기 캐싱 타이머 등록 ===
    // [최적화] Thundering Herd 방지: 모든 NPC가 동시에 EQS를 돌려 프레임 스파이크가 튀지 않도록,
    // 초기 시작 시간에 0.1~1.0초 난수 편차(Staggering)를 적용합니다.
    if (TacticalCoverQuery)
    {
        const float RandomStartDelay = 1.0f + FMath::RandRange(0.1f, 1.0f);
        GetWorldTimerManager().SetTimer(
            EQSRefreshTimerHandle,
            this,
            &ASmartNPC::RefreshTacticalEQS,
            1.0f,    // 이후 1초 간격 반복
            true,    // 루프
            RandomStartDelay // 첫 실행만 난수 지연
        );
        UE_LOG(LogTemp, Log, TEXT("[SmartNPC:%s] EQS 비동기 캐싱 타이머 시작 (%.2f초 후 첫 실행)"), *AgentID, RandomStartDelay);
    }
}

void ASmartNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // EQS 타이머 해제 (레벨 전환/액터 삭제 시 콜백 누수 방지)
    GetWorldTimerManager().ClearTimer(EQSRefreshTimerHandle);

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->UnregisterNPC(AgentID);
        }
    }

    Super::EndPlay(EndPlayReason);
}

// === Facade: 외부 호출을 컴포넌트로 전달 ===

void ASmartNPC::ExecuteActionBatch(const FActionBatch& Batch)
{
    if (ActionComponent)
    {
        ActionComponent->ExecuteActionBatch(Batch);
    }
}

/**
 * [Time-Slicing 콜백] NPCManager의 0.5초 글로벌 타이머가 주기적으로 호출합니다.
 * 현재 NPC의 상태를 FGameStateData에 채워 Python 백엔드로 전송합니다.
 * EQS 결과는 별도 타이머(RefreshTacticalEQS)로 비동기 캐싱된 값을 그대로 읽습니다.
 */
void ASmartNPC::CollectAndSendStateUpdate()
{
    // NPC가 유효한 상태가 아니면 불필요한 직렬화를 건너뜁니다.
    if (!IsValid(this) || AgentID.IsEmpty())
    {
        return;
    }
    
    //TODO: 일반적이지 않은 상황만을 전송하도록 변경
    // 현재 NPC의 상태를 FGameStateData에 채웁니다.
    FGameStateData StateSnapshot;
    StateSnapshot.OwnerAgentID = AgentID;
    StateSnapshot.OwnerLocation = GetActorLocation();

    // === EQS 캐시 결과 적용 ===
    // RefreshTacticalEQS() 타이머가 백그라운드에서 갱신해둔 캐시를 그대로 복사합니다.
    // 여기서는 어떤 매트 연산도 일어나지 않으므로 프레임 비용이 0에 가깍습니다.
    StateSnapshot.EQSResults = CachedEQSResults;

    // StateComponent로부터 현재 행동 모드를 가져옵니다.
    if (StateComponent)
    {
        // 기본 위협 수준을 StateComponent의 체력 기반으로 추론합니다.
        const FCharacterAttributes CurrentStats = StateComponent->GetCurrentStats();
        const float HealthRatio = CurrentStats.Resources.Health / FMath::Max(1.f, CurrentStats.Resources.MaxHealth);
        if (HealthRatio < 0.3f)
        {
            StateSnapshot.ThreatLevel = TEXT("High");
        }
        else if (HealthRatio < 0.6f)
        {
            StateSnapshot.ThreatLevel = TEXT("Medium");
        }
        else
        {
            StateSnapshot.ThreatLevel = TEXT("Low");
        }
    }

    // NPCManager를 통해 직렬화된 JSON을 Python 백엔드로 전송합니다.
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->SendStateToMCP(StateSnapshot);
        }
    }
}

// ============================================================
// EQS 비동기 캐싱 시스템 (백그라운드 타이머로 동작)
// ============================================================

/**
 * 1초 주기로 호출되는 EQS 비동기 실행 함수.
 * CollectAndSendStateUpdate(0.5초)와 독립된 주기로 돌며, 결과는 CachedEQSResults에 저장됩니다.
 */
void ASmartNPC::RefreshTacticalEQS()
{
    // EQS 에셋이 에디터에서 할당되지 않았거나, 액터가 파괴 중이면 건너뜄니다.
    if (!TacticalCoverQuery || !IsValid(this))
    {
        return;
    }

    UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(GetWorld());
    if (!EQSManager)
    {
        return;
    }

    // 비동기 EQS 쿼리 실행 - 결과가 준비되면 OnCoverQueryFinished 콜백이 GameThread에서 호출됩니다.
    FEnvQueryRequest QueryRequest(TacticalCoverQuery, this);
    QueryRequest.Execute(EEnvQueryRunMode::AllMatching, 
        FQueryFinishedSignature::CreateUObject(this, &ASmartNPC::OnCoverQueryFinished));
}

/**
 * EQS 쿼리 완료 콜백.
 * [안전장치] IsValid(this) 챀크로 액터 파괴 후 콜백 도달 시의 크래시를 예방합니다.
 * 최대 3개 좌표만 캐싱하여 Data Diet를 적용합니다.
 */
void ASmartNPC::OnCoverQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
    // [보안] 쿼리 도중 NPC가 파괴(Destroy)되었을 경우 크래시 방지
    if (!IsValid(this))
    {
        return;
    }

    if (!Result.IsValid() || !Result->IsSuccessful())
    {
        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC:%s] EQS 쿼리 실패 또는 결과 없음"), *AgentID);
        return;
    }

    // 기존 캐시 비우고 새 결과로 갱신
    CachedEQSResults.Empty();

    TArray<FVector> Locations;
    Result->GetAllAsLocations(Locations);

    // 최대 3개까지만 캐싱 (LLM 토큰 절약 및 패킷 최소화)
    // TODO: 현재는 점수 상위 3개를 무차별로 저장하지만, 전술적 목적별로 분류해야 합니다.
    //       - [0] 가장 안전한 장소 (이성적 판단 - Cover/방어 최적)
    //       - [1] 가장 유리한 장소 (공격적 판단 - 사선 확보/플랭킹)
    //       - [2] 가장 가까운 출구 (도주 판단 - 탈출 경로)
    //       → 단일 쿼리 결과를 후처리해 분류하는 로직 필요.
    const int32 MaxItems = FMath::Min(Locations.Num(), 3);
    for (int32 i = 0; i < MaxItems; ++i)
    {
        FEQSResult NewResult;
        NewResult.QueryTag = TEXT("Cover");
        NewResult.BestLocation = Locations[i];
        NewResult.Score = Result->GetItemScore(i);

        CachedEQSResults.Add(NewResult);
    }

    UE_LOG(LogTemp, Verbose, TEXT("[SmartNPC:%s] EQS 캐시 갱신 완료: %d개 좌표 저장됨"), *AgentID, CachedEQSResults.Num());
}

/**
 * [Offline Fallback 트리거] NPCManager가 WebSocket 연결 상태 변화를 감지했을 때 호출합니다.
 * Behavior Tree의 Selector 노드가 'IsConnected' 블랙보드 키를 감지해 Local BT로 자동 분기합니다.
 */
void ASmartNPC::SetBlackboardBool(const FString& KeyName, bool bValue)
{
    ASmartNPCAIController* AICtrl = Cast<ASmartNPCAIController>(GetController());
    if (!AICtrl)
    {
        return;
    }

    UBlackboardComponent* BlackboardComp = AICtrl->GetBlackboardComponent();
    if (!BlackboardComp)
    {
        return;
    }

    BlackboardComp->SetValueAsBool(FName(*KeyName), bValue);
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Blackboard key '%s' set to %s on NPC '%s'"),
        *KeyName,
        bValue ? TEXT("true") : TEXT("false"),
        *AgentID);
}


void ASmartNPC::ClearPhysicalState()
{
    StopAnimMontage();
    if (AController* C = GetController())
    {
        C->StopMovement();
    }
}

void ASmartNPC::OnActionCompleted()
{
    if (ActionComponent)
    {
        ActionComponent->OnActionCompleted();
    }
}

FCharacterAttributes ASmartNPC::GetStats() const
{
    // StateComponent가 반드시 존재한다고 가정 (생성자에서 보장)
    return StateComponent ? StateComponent->GetCurrentStats() : FCharacterAttributes();
}

// === TakeDamage: UE5 Actor Override → StateComponent에 위임 ===

float ASmartNPC::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, 
    AController* EventInstigator, AActor* DamageCauser)
{
    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    
    // 2초 이내에 피격된 경우 처리를 무시하여 과도한 인지/데미지 계산 방지
    if (StateComponent && GetWorld()->GetTimeSeconds() - StateComponent->LastHitTime > 2.0f)
    {
        StateComponent->ApplyDamage(ActualDamage);
        StateComponent->RequestEmergencyCognition(TEXT("Hit"), 
            FString::Printf(TEXT("Took %.1f Damage"), ActualDamage));
    }

    return ActualDamage;
}


// ==========================================
// Debug Functions (에디터에서 버튼 클릭으로 테스트)
// ==========================================

namespace
{
    // [의도(Why)] 하드코딩된 JSON 문자열을 생성하여 NPCManager를 통해 통합 테스트를 수행하는 헬퍼 함수
    void DispatchDebugJson(ASmartNPC* NPCInstance, const FString& Mode, const FString& ActionsJson)
    {
        if (!NPCInstance) return;

        FString MockJson = FString::Printf(TEXT(R"({
    "Mode": "%s",
    "ActionBatches": {
        "%s": {
            "AgentID": "%s",
            "Mode": "%s",
            "Actions": [
                %s
            ]
        }
    }
})"), *Mode, *NPCInstance->AgentID, *NPCInstance->AgentID, *Mode, *ActionsJson);

        UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] DispatchDebugJson: Sending to NPCManager\n%s"), *MockJson);

        if (UGameInstance* GameInst = NPCInstance->GetGameInstance())
        {
            if (UNPCManager* NPCManager = GameInst->GetSubsystem<UNPCManager>())
            {
                NPCManager->OnWebSocketMessageReceived(MockJson);
            }
        }
    }
}

void ASmartNPC::Debug_Test_Social_Dialogue()
{
    // [의도(Why)] 사교 모드(Social)로 전환 후 대화(Dialogue) 행동이 올바르게 큐잉되어 실행되는지 확인합니다.
    FString ActionsJson = TEXT(R"({
                    "ActionType": "Dialogue",
                    "FacialState": "Neutral",
                    "Parameters": {
                        "TargetID": "Player",
                        "text": "Hello! This is a debug test."
                    }
                })");
    DispatchDebugJson(this, TEXT("Social"), ActionsJson);
}

void ASmartNPC::Debug_Test_Common_Move()
{
    // [의도(Why)] 기본 모드(Common) 상태에서 액터(Player)를 향한 이동(Move) 내비게이션 처리를 검증합니다.
    FString ActionsJson = TEXT(R"({
                    "ActionType": "Move",
                    "Parameters": {
                        "TargetID": "Player"
                    }
                })");
    DispatchDebugJson(this, TEXT("Common"), ActionsJson);
}

void ASmartNPC::Debug_Test_Combat_Attack()
{
    // [의도(Why)] 전투 모드(Combat) 상태에서 대상체(Player)를 향한 공격 몽타주 재생이 트리거되는지 확인합니다.
    FString ActionsJson = TEXT(R"({
                    "ActionType": "Attack",
                    "Parameters": {
                        "TargetID": "Player"
                    }
                })");
    DispatchDebugJson(this, TEXT("Combat"), ActionsJson);
}

void ASmartNPC::Debug_Test_Orchestra_Pipeline()
{
    // [의도(Why)] 서버에서 수신되는 다중 에이전트 명령 포맷(FModeActionRequest)이 
    // NPCManager를 통해 각 NPC에게 올바르게 분배 및 파싱되는지 시뮬레이션합니다.

    // 1. 하드코딩된 완벽한 JSON 문자열 생성 (파이썬 서버에서 내려오는 것과 100% 동일한 형태)
    FString MockJson = FString::Printf(TEXT(R"({
    "Mode": "Social",
    "ActionBatches": {
        "%s": {
            "AgentID": "%s",
            "Mode": "Social",
            "Actions": [
                {
                    "ActionType": "Dialogue",
                    "FacialState": "Happy",
                    "Parameters": {
                        "text": "Pipeline Test: Hello!"
                    }
                },
                {
                    "ActionType": "Wait",
                    "FacialState": "Happy",
                    "Parameters": {
                        "duration": "1.5"
                    }
                }
            ]
        }
    }
})"), *AgentID, *AgentID);

    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Testing Orchestra Pipeline with JSON: %s"), *MockJson);

    if (UGameInstance* GameInst = GetGameInstance())
    {
        if (UNPCManager* NPCManager = GameInst->GetSubsystem<UNPCManager>())
        {
            NPCManager->OnWebSocketMessageReceived(MockJson);
        }
    }
}