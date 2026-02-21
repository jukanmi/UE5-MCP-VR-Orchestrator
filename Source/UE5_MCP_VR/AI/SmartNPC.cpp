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

// === Facade: Execute* Wrappers → ActionComponent에 위임 ===

void ASmartNPC::ExecuteMoveToLocation(FVector TargetLocation, EMoveType SpeedType, float AcceptanceRadius)
{
    if (ActionComponent) ActionComponent->ExecuteMoveToLocation(TargetLocation, SpeedType, AcceptanceRadius);
}

void ASmartNPC::ExecuteKeepDistance(AActor* TargetActor, EMoveType SpeedType, float Distance)
{
    if (ActionComponent) ActionComponent->ExecuteKeepDistance(TargetActor, SpeedType, Distance);
}

void ASmartNPC::ExecuteDialogue(const FString& DialogueText, const FString& EmotionID)
{
    if (ActionComponent)
    {
        // FString → EFacialState 변환
        EFacialState Emotion = EFacialState::Neutral;
        const UEnum* FacialEnum = StaticEnum<EFacialState>();
        if (FacialEnum)
        {
            int64 Val = FacialEnum->GetValueByNameString(EmotionID);
            if (Val != INDEX_NONE) Emotion = (EFacialState)Val;
        }
        ActionComponent->ExecuteDialogue(DialogueText, Emotion);
    }
}

void ASmartNPC::ExecuteWait(float Duration)
{
    if (ActionComponent) ActionComponent->ExecuteWait(Duration);
}

void ASmartNPC::ExecuteFaceRotate(FVector TargetLocation, float TurnSpeed)
{
    if (ActionComponent) ActionComponent->ExecuteFaceRotate(TargetLocation, TurnSpeed);
}

void ASmartNPC::StopAllActions()
{
    if (ActionComponent) ActionComponent->StopAllActions();
}

void ASmartNPC::ExecutePerformAttack(AActor* TargetActor, const FString& AttackType)
{
    if (ActionComponent) ActionComponent->ExecutePerformAttack(AttackType);
}

void ASmartNPC::ExecuteDefend(bool bStartDefend)
{
    if (ActionComponent) ActionComponent->ExecuteDefend(bStartDefend);
}

void ASmartNPC::ExecuteDodge()
{
    if (ActionComponent) ActionComponent->ExecuteDodge();
}

void ASmartNPC::ExecuteEquip(const FString& ItemID)
{
    if (ActionComponent) ActionComponent->ExecuteEquip(ItemID);
}

void ASmartNPC::ExecuteUnequip(const FString& ItemID)
{
    if (ActionComponent) ActionComponent->ExecuteUnequip(ItemID);
}

//Case SitDown SitUp, LieDown, LieUp, Pickup, Drop, Eat, Wear, Equip, Unequip, Clean, Repair, Read, Pray, Dance, Sing, HandSignal, Emote
void ASmartNPC::ExecuteInteraction(const FString& InteractionType, AActor* TargetActor, const FString& TargetID, const FString& ExtraParams)
{
    if (ActionComponent) ActionComponent->ExecuteInteraction(InteractionType, TargetActor, TargetID, ExtraParams);
}

void ASmartNPC::ExecuteEmote(const FString& EmoteName)
{
    if (ActionComponent) ActionComponent->ExecuteEmote(EmoteName);
}

void ASmartNPC::ExecuteHandSignal(const FString& SignalName)
{
    if (ActionComponent) ActionComponent->ExecuteHandSignal(SignalName);
}

void ASmartNPC::ExecuteKeepDistance(AActor* TargetActor, float Distance, float Speed)
{
    
}

// ==========================================
// Debug Functions (에디터에서 버튼 클릭으로 테스트)
// ==========================================

void ASmartNPC::Debug_Test_Social_Dialogue()
{
    // [의도(Why)] 대화/사교(Social) 모드로 전환되었을 때, 매핑된 ActionQueue 시스템이
    // 정상적으로 동작하며 에디터 환경에서 패치(Batch)가 올바르게 실행되는지 확인합니다.
    FGameAction Action;
    Action.ActionType = NPCActionKeys::Action_Dialogue;
    Action.TargetID = TEXT("Player");
    Action.Parameters.Add(NPCActionKeys::Key_Text, TEXT("Hello! This is a debug test."));
    Action.Parameters.Add(NPCActionKeys::Key_Emotion, NPCActionKeys::Value_Neutral);
    Action.BehaviorMode = NPCActionKeys::Mode_Social;

    FActionBatch Batch;
    Batch.AgentID = AgentID;
    Batch.Actions.Add(Action);
    
    ExecuteActionBatch(Batch);
}

void ASmartNPC::Debug_Test_Common_Move()
{
    // [의도(Why)] 가장 기본적인 행동인 '이동(Move)'이 Common 모드 안에서
    // 타겟(Player)을 향해 올바르게 내비게이션(NavMesh)을 타고 이동하는지 검증합니다.
    FGameAction Action;
    Action.ActionType = NPCActionKeys::Action_Move;
    Action.TargetID = TEXT("Player");
    Action.BehaviorMode = NPCActionKeys::Mode_Common;

    FActionBatch Batch;
    Batch.AgentID = AgentID;
    Batch.Actions.Add(Action);
    
    ExecuteActionBatch(Batch);
}

void ASmartNPC::Debug_Test_Combat_Attack()
{
    // [의도(Why)] 전투(Combat) 모드 시, Attack 액션을 큐에 담았을 때 
    // NPC가 타겟을 인식하고 공격 애니메이션(몽타주)을 실행하는지 검증합니다.
    FGameAction Action;
    Action.ActionType = NPCActionKeys::Action_Attack;
    Action.TargetID = TEXT("Player");
    Action.BehaviorMode = NPCActionKeys::Mode_Combat;

    FActionBatch Batch;
    Batch.AgentID = AgentID;
    Batch.Actions.Add(Action);
    
    ExecuteActionBatch(Batch);
}

void ASmartNPC::Debug_Test_Orchestra_Pipeline()
{
    // [의도(Why)] 백엔드(서버)에서 여러 NPC의 스크립트가 포함된 JSON이 전달되었을 때,
    // NPCManager가 파싱하여 각 액터에게 정상적으로 분배(Dispatch)하는지 통합 시뮬레이션합니다.
    TArray<TSharedPtr<FJsonValue>> AgentList;

    // 1. 본인(SmartNPC) 에이전트를 위한 대화 + 대기 콤보 액션 스크립트 작성
    TSharedPtr<FJsonObject> MyAgent = MakeShareable(new FJsonObject);
    MyAgent->SetStringField("agent_id", AgentID);

    TArray<TSharedPtr<FJsonValue>> MyActions;

    TSharedPtr<FJsonObject> Act1 = MakeShareable(new FJsonObject);
    Act1->SetStringField("action_type", NPCActionKeys::Action_Dialogue);
    Act1->SetStringField("text", "Pipeline Test: Hello!");
    Act1->SetStringField("emotion", "Happy");
    Act1->SetStringField("behavior_mode", NPCActionKeys::Mode_Social);
    Act1->SetStringField("facial_state", "Happy");
    MyActions.Add(MakeShareable(new FJsonValueObject(Act1)));

    TSharedPtr<FJsonObject> Act2 = MakeShareable(new FJsonObject);
    Act2->SetStringField("action_type", NPCActionKeys::Action_Wait);
    Act2->SetStringField("duration", "1.5");
    Act2->SetStringField("behavior_mode", NPCActionKeys::Mode_Social);
    MyActions.Add(MakeShareable(new FJsonValueObject(Act2)));

    MyAgent->SetArrayField("actions", MyActions);
    AgentList.Add(MakeShareable(new FJsonValueObject(MyAgent)));

    // 2. 화면에 존재하지 않는(Ghost) NPC 정보가 포함되었을 때 에러 없이 무시되는지 방어 케이스 작성
    TSharedPtr<FJsonObject> GhostAgent = MakeShareable(new FJsonObject);
    GhostAgent->SetStringField("agent_id", "Ghost_NPC");
    TArray<TSharedPtr<FJsonValue>> GhostActions;
    TSharedPtr<FJsonObject> GhostAct = MakeShareable(new FJsonObject);
    GhostAct->SetStringField("action_type", NPCActionKeys::Action_Move);
    GhostAct->SetStringField("behavior_mode", NPCActionKeys::Mode_Combat);
    GhostActions.Add(MakeShareable(new FJsonValueObject(GhostAct)));
    GhostAgent->SetArrayField("actions", GhostActions);
    AgentList.Add(MakeShareable(new FJsonValueObject(GhostAgent)));

    // 직렬화 (Serialization)
    FString MockJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&MockJson);
    FJsonSerializer::Serialize(AgentList, Writer);

    UE_LOG(LogTemp, Log, TEXT("[Debug] Testing Orchestra Pipeline with JSON: %s"), *MockJson);

    // Reflection을 통해 NPCManager의 HandleMessage를 직접 트리거
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            UFunction* Func = Manager->FindFunction(TEXT("HandleMessage"));
            if (Func)
            {
                struct FParams { FString Msg; };
                FParams Params;
                Params.Msg = MockJson;
                Manager->ProcessEvent(Func, &Params);
            }
        }
    }
}

void ASmartNPC::Debug_Test_Interaction(FString InteractionKey, FString ExtraParams)
{
    // [의도(Why)] NPCActionComponent 내부적으로 구현된 ExecuteInteraction이 수많은 키(SitDown, LieDown 등)를 
    // 정상적으로 분기하고 알맞은 애니메이션 몽타주를 매핑하는지 강제 실행해보기 위함입니다.
    
    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Debug_Test_Interaction: Key=%s, Extra=%s"), *InteractionKey, *ExtraParams);
    
    if (ActionComponent)
    {
        // 물리적 Actor(Seat/Bed 등) 없이 "DebugTarget"이라는 더미 타겟으로 로직 흐름과 몽타주만 테스트합니다.
        ActionComponent->ExecuteInteraction(InteractionKey, nullptr, TEXT("DebugTarget"), ExtraParams);
    }
}