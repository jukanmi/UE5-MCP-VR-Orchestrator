/**
 * SmartNPC.cpp - Lightweight Facade Implementation
 * 
 * [역할] 초기화, 등록/해제, TakeDamage 후킹, Debug 함수만 담당.
 * 모든 행동/상태 로직은 NPCActionComponent와 NPCStateComponent에 위임.
 */

#include "SmartNPC.h"
#include "NPCManager.h"
#include "SmartNPCAIController.h"
#include "NPCActionKeys.h"
#include "../Component/NPCStateComponent.h"
#include "../Component/NPCActionComponent.h"
#include "../Component/NPCInventoryComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"

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

FCharacterAttributes& ASmartNPC::GetStats() const
{
    // StateComponent가 반드시 존재한다고 가정 (생성자에서 보장)
    return StateComponent->CurrentStats;
}

// === TakeDamage: UE5 Actor Override → StateComponent에 위임 ===

float ASmartNPC::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, 
    AController* EventInstigator, AActor* DamageCauser)
{
    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

    if (StateComponent)
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