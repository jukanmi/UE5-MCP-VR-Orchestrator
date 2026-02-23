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
    if (ActionComponent) ActionComponent->BaseMoveToLocation(TargetLocation, SpeedType, AcceptanceRadius);
}

void ASmartNPC::ExecuteKeepDistance(AActor* TargetActor, EMoveType SpeedType, float Distance)
{
    if (ActionComponent) ActionComponent->ExecuteKeepDistance(TargetActor, SpeedType, Distance);
}

void ASmartNPC::ExecuteDialogue(const FString& DialogueText, const EFacialState Emotion)
{
    if (ActionComponent)
    {
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

void ASmartNPC::ExecuteStop()
{
    if (ActionComponent) ActionComponent->StopAllActions();
}

void ASmartNPC::ExecutePerformAttack(AActor* TargetActor, const EAttackType AttackType)
{
    if (ActionComponent) ActionComponent->ExecutePerformAttack(TargetActor, AttackType);
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
void ASmartNPC::ExecuteInteraction(const EAction InteractionType, AActor* TargetActor, const FString& TargetID)
{
    if (ActionComponent) ActionComponent->ExecuteInteraction(InteractionType, TargetActor, TargetID);
}

void ASmartNPC::ExecuteEmote(const FString& EmoteName)
{
    if (ActionComponent) ActionComponent->ExecuteEmote(EmoteName);
}

void ASmartNPC::ExecuteHandSignal(const FString& SignalName)
{
    if (ActionComponent) ActionComponent->ExecuteHandSignal(SignalName);
}

// ============================================================================
// [EAction 래퍼 함수 (Action Wrappers)]
// ============================================================================

// --- [1] Common Behaviors ---
void ASmartNPC::ExecuteIdle() { if(ActionComponent) ActionComponent->ExecuteIdle(); }
void ASmartNPC::ExecuteMove(FVector Location, AActor* TargetActor, EMoveType SpeedType) { if(ActionComponent) ActionComponent->ExecuteMove(Location, TargetActor, SpeedType); }
void ASmartNPC::ExecuteFollow(AActor* TargetActor, EMoveType SpeedType) { if(ActionComponent) ActionComponent->ExecuteFollow(TargetActor, SpeedType); }
void ASmartNPC::ExecuteTurnTo(FVector Location, AActor* TargetActor) { if(ActionComponent) ActionComponent->ExecuteTurnTo(Location, TargetActor); }

void ASmartNPC::ExecuteScan(FVector Location, AActor* TargetActor) { if(ActionComponent) ActionComponent->ExecuteScan(Location, TargetActor); }
void ASmartNPC::ExecuteUseItem(const FString& ItemID) { if(ActionComponent) ActionComponent->ExecuteUseItem(ItemID); }
void ASmartNPC::ExecuteEquipAction(const FString& ItemID) { if(ActionComponent) ActionComponent->ExecuteEquipAction(ItemID); }
void ASmartNPC::ExecuteUnequipAction(const FString& ItemID) { if(ActionComponent) ActionComponent->ExecuteUnequipAction(ItemID); }

// --- [2] Combat Behaviors ---
void ASmartNPC::ExecuteAttackAction(AActor* TargetActor) { if(ActionComponent) ActionComponent->ExecuteAttackAction(TargetActor); }
void ASmartNPC::ExecuteBlock(AActor* TargetActor) { if(ActionComponent) ActionComponent->ExecuteBlock(TargetActor); }
void ASmartNPC::ExecuteDodgeAction(FVector Direction) { if(ActionComponent) ActionComponent->ExecuteDodgeAction(Direction); }
void ASmartNPC::ExecuteFlee(FVector Location) { if(ActionComponent) ActionComponent->ExecuteFlee(Location); }
void ASmartNPC::ExecuteSignalAllies(const FString& HandSign) { if(ActionComponent) ActionComponent->ExecuteSignalAllies(HandSign); }

// --- [3] Social Behaviors ---
void ASmartNPC::ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, const FString& GetItemID) { if(ActionComponent) ActionComponent->ExecuteTrade(TargetActor, GiveItemID, GetItemID); }
void ASmartNPC::ExecuteGiveItem(AActor* TargetActor, const FString& ItemID) { if(ActionComponent) ActionComponent->ExecuteGiveItem(TargetActor, ItemID); }
void ASmartNPC::ExecuteComfort(AActor* TargetActor) { if(ActionComponent) ActionComponent->ExecuteComfort(TargetActor); }
void ASmartNPC::ExecuteHandObject(const FString& ItemID) { if(ActionComponent) ActionComponent->ExecuteHandObject(ItemID); }

// --- [4] Task Behaviors ---
void ASmartNPC::ExecutePickUp(FVector Location) { if(ActionComponent) ActionComponent->ExecutePickUp(Location); }
void ASmartNPC::ExecuteDrop(const FString& ItemID) { if(ActionComponent) ActionComponent->ExecuteDrop(ItemID); }
void ASmartNPC::ExecuteCraft(const TArray<FString>& ItemIDs) { if(ActionComponent) ActionComponent->ExecuteCraft(ItemIDs); }
void ASmartNPC::ExecuteRepair(const FString& ItemID) { if(ActionComponent) ActionComponent->ExecuteRepair(ItemID); }

// --- [5] Investigation Behaviors ---
void ASmartNPC::ExecuteInvestigate(FVector Location) { if(ActionComponent) ActionComponent->ExecuteInvestigate(Location); }
void ASmartNPC::ExecuteScout(FVector StartLocation, FVector EndLocation) { if(ActionComponent) ActionComponent->ExecuteScout(StartLocation, EndLocation); }

// --- [6] Lifestyle Behaviors ---
void ASmartNPC::ExecuteSit(AActor* TargetEntity) { if(ActionComponent) ActionComponent->ExecuteSit(TargetEntity); }
void ASmartNPC::ExecuteSleep(AActor* TargetEntity) { if(ActionComponent) ActionComponent->ExecuteSleep(TargetEntity); }
void ASmartNPC::ExecuteClean(FVector Location, float Radius) { if(ActionComponent) ActionComponent->ExecuteClean(Location, Radius); }
void ASmartNPC::ExecuteRead(AActor* TargetEntity) { if(ActionComponent) ActionComponent->ExecuteRead(TargetEntity); }
void ASmartNPC::ExecutePray(FVector Location) { if(ActionComponent) ActionComponent->ExecutePray(Location); }
void ASmartNPC::ExecuteDance(const FString& DanceName) { if(ActionComponent) ActionComponent->ExecuteDance(DanceName); }
void ASmartNPC::ExecuteSing(const FString& SingName) { if(ActionComponent) ActionComponent->ExecuteSing(SingName); }

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
                NPCManager->HandleMessage(MockJson);
            }
        }
    }
}

void ASmartNPC::Debug_Test_Social_Dialogue()
{
    // [의도(Why)] 사교 모드(Social)로 전환 후 대화(Dialogue) 행동이 올바르게 큐잉되어 실행되는지 확인합니다.
    FString ActionsJson = TEXT(R"({
                    "action_type": "Dialogue",
                    "target_id": "Player",
                    "text": "Hello! This is a debug test.",
                    "emotion": "Neutral"
                })");
    DispatchDebugJson(this, TEXT("Social"), ActionsJson);
}

void ASmartNPC::Debug_Test_Common_Move()
{
    // [의도(Why)] 기본 모드(Common) 상태에서 액터(Player)를 향한 이동(Move) 내비게이션 처리를 검증합니다.
    FString ActionsJson = TEXT(R"({
                    "action_type": "Move",
                    "target_id": "Player"
                })");
    DispatchDebugJson(this, TEXT("Common"), ActionsJson);
}

void ASmartNPC::Debug_Test_Combat_Attack()
{
    // [의도(Why)] 전투 모드(Combat) 상태에서 대상체(Player)를 향한 공격 몽타주 재생이 트리거되는지 확인합니다.
    FString ActionsJson = TEXT(R"({
                    "action_type": "Attack",
                    "target_id": "Player"
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
                    "text": "Pipeline Test: Hello!",
                    "FacialState": "Happy"
                },
                {
                    "ActionType": "Wait",
                    "duration": "1.5"
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
            NPCManager->HandleMessage(MockJson);
        }
    }
}

void ASmartNPC::Debug_Test_Interaction(EAction Action, FString ExtraParams)
{
    // [의도(Why)] 액션 컴포넌트 내부에서 상호작용 행동(앉기, 줍기 등)이 
    // 매핑된 애니메이션 몽타주와 함께 올바르게 재생되는지 강제 트리거합니다.
    
    const FString ActionName = UEnum::GetValueAsString(Action);
    UE_LOG(LogTemp, Warning, TEXT("[SmartNPC] Debug_Test_Interaction: Action=%s, Extra=%s"), *ActionName, *ExtraParams);
    
    if (ActionComponent)
    {
        // 물리적 공간 지시자(Seat 등)가 없는 상황에서도 흐름 및 몽타주 연동 기능만 단독 테스트하기 위해 더미 타겟 설정
        ActionComponent->ExecuteInteraction(Action, nullptr, TEXT("DebugTarget"));
    }
}