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

#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"

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

    // AI Perception Stimuli Source (NPC가 시각/청각 소스 역할을 할 수 있도록 함)
    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    if (StimuliSource)
    {
        // 시각과 청각 감지 대상으로 등록
        StimuliSource->RegisterForSense(TSubclassOf<UAISense_Sight>());
        StimuliSource->RegisterForSense(TSubclassOf<UAISense_Hearing>());
        StimuliSource->RegisterWithPerceptionSystem();
    }
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

    // TODO: 시각/청각 자극 감지용 이벤트 리스너(OnTargetPerceptionUpdated 등) 등록 예정
}

void ASmartNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{


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

    // === 시각/청각 인지(Perception) 시스템 적용 ===
    if (ASmartNPCAIController* AIController = Cast<ASmartNPCAIController>(GetController()))
    {
        if (UAIPerceptionComponent* PerceptionComp = AIController->GetAIPerceptionComponent())
        {
            TArray<AActor*> SensedActors;
            PerceptionComp->GetKnownPerceivedActors(nullptr, SensedActors);
            
            FAISenseID SightID = UAISense::GetSenseID<UAISense_Sight>();
            FAISenseID HearingID = UAISense::GetSenseID<UAISense_Hearing>();
            float CurrentTime = GetWorld()->GetTimeSeconds();

            for (AActor* SensedActor : SensedActors)
            {
                if (!IsValid(SensedActor) || SensedActor == this) continue;

                FActorPerceptionBlueprintInfo Info;
                if (PerceptionComp->GetActorsPerception(SensedActor, Info))
                {
                    // 현재 활성화된(감지된) 자극 중 가장 최근 것을 찾습니다.
                    for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
                    {
                        if (!Stimulus.WasSuccessfullySensed()) continue;

                        FPerceptionData PercData;
                        PercData.Location = SensedActor->GetActorLocation();
                        PercData.Distance = FVector::Dist(GetActorLocation(), PercData.Location);

                        bool bIsIdentified = false;
                        APawn* IdentifiedPawn = nullptr; // 최종적으로 식별된 대상

                        // 1. 조도 기반 동적 임계값 보정 (향후 확장을 위해 1.0f 처리)
                        float CurrentLightFactor = 1.0f; 
                        float DynamicIdentifyThreshold = BaseIdentificationRadius * CurrentLightFactor;

                        // 2. 감각별 분기 처리
                        if (Stimulus.Type == SightID)
                        {
                            // 시각 처리
                            TWeakObjectPtr<AActor> WeakActor(SensedActor);
                            
                            // 플리커링을 차단하기 위한 버퍼를 주기 위해선 '이전에 본 적 있는 대상'인지 판별해야 함.
                            // 맵의 Key가 Actor이므로 O(1) 상수 시간 검색으로 즉시 기존 추적 상태임을 검색함.
                            bool bWasAlreadyIdentified = KnownTargetsMap.Contains(WeakActor);
                            
                            // 목표물이 경계선에서 들어왔다 나갔다 하여 인식이 끊기는 것을 막기 위한 히스테리시스(여유치) 제공
                            float ThresholdToUse = bWasAlreadyIdentified ? (DynamicIdentifyThreshold + 200.f) : DynamicIdentifyThreshold;

                            if (PercData.Distance <= ThresholdToUse)
                            {
                                bIsIdentified = true;
                                IdentifiedPawn = Cast<APawn>(SensedActor);

                                // 시각에 들어왔으므로 공간 지도(Map) 갱신
                                if (IdentifiedPawn)
                                {
                                    FKnownTargetInfo KnownInfo;
                                    KnownInfo.LastLocation = SensedActor->GetActorLocation();
                                    KnownInfo.LastSeenTime = CurrentTime;
                                    
                                    //  Actor를 키로 사용하여 시각 정보가 일치할 때 가장 빠르게 최신 위치를 갱신함.
                                    KnownTargetsMap.Add(WeakActor, KnownInfo);
                                }
                            }
                            PercData.SenseType = ESenseType::Sight;
                        }
                        else if (Stimulus.Type == HearingID)
                        {
                            // [청각 처리] O(N) 공간 대조
                            TWeakObjectPtr<AActor> BestMatchActor = nullptr;
                            float ClosestDistSq = FMath::Square(HearingAssociationRadius);

                            for (auto It = KnownTargetsMap.CreateIterator(); It; ++It)
                            {
                                TWeakObjectPtr<AActor> SavedActor = It.Key();
                                FKnownTargetInfo& KnownInfo = It.Value();

                                //  UE5의 파괴된 액터 참조로 인한 크래시(Memory Leak/Dangling) 방지를 위해 실시간으로 객체 유효성을 검사해 청소함.
                                if (!SavedActor.IsValid())
                                {
                                    It.RemoveCurrent();
                                    continue;
                                }

                                //  오래된 기억(과거 위치)에 기반해 판단하면 환각 현상이 일어나므로 시간(TTL)이 초과된 데이터는 신뢰성 판단을 위해 제거함.
                                if (CurrentTime - KnownInfo.LastSeenTime > TargetMemoryTTL)
                                {
                                    It.RemoveCurrent();
                                    continue;
                                }

                                //  발생한 청각 소스의 장소와 가장 최근 목격된 액터 위치의 거리차이를 연산하여, 동일 인체의 소음 발생인지 유추함.
                                float DistSq = FVector::DistSquared(Stimulus.StimulusLocation, KnownInfo.LastLocation);
                                if (DistSq < ClosestDistSq)
                                {
                                    ClosestDistSq = DistSq;
                                    BestMatchActor = SavedActor;
                                }
                            }

                            if (BestMatchActor.IsValid())
                            {
                                bIsIdentified = true;
                                IdentifiedPawn = Cast<APawn>(BestMatchActor.Get());
                            }
                            else if (Stimulus.Tag == FName("Voice")) 
                            {
                                // 연동 실패라도 말소리라면 식별 부여
                                IdentifiedPawn = Cast<APawn>(SensedActor);
                                if (IdentifiedPawn) bIsIdentified = true;
                            }
                            
                            PercData.SenseType = ESenseType::Hearing;
                        }
                        else
                        {
                            PercData.SenseType = ESenseType::Other;
                        }

                        // 3. 최종 식별 이름 할당
                        if (bIsIdentified && IdentifiedPawn)
                        {
                            PercData.TargetID = IdentifiedPawn->GetName(); 
                        }
                        else
                        {
                            PercData.TargetID = TEXT("unknown");
                        }

                        StateSnapshot.PerceivedTargets.Add(PercData);
                        break; // 같은 액터에 대한 여러 자극 중 하나만 기록
                    }
                }
            }
        }
    }

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
