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
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

ASmartNPC::ASmartNPC()
{
    PrimaryActorTick.bCanEverTick = true;
    AgentID = TEXT("UnknownAgent");
    AIControllerClass = ASmartNPCAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

    StateComponent     = CreateDefaultSubobject<UNPCStateComponent>(TEXT("StateComponent"));
    ActionComponent    = CreateDefaultSubobject<UNPCActionComponent>(TEXT("ActionComponent"));
    InventoryComponent = CreateDefaultSubobject<UNPCInventoryComponent>(TEXT("InventoryComponent"));

    StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
    if (StimuliSource)
    {
        StimuliSource->RegisterForSense(TSubclassOf<UAISense_Sight>());
        StimuliSource->RegisterForSense(TSubclassOf<UAISense_Hearing>());
        StimuliSource->RegisterWithPerceptionSystem();
    }
}

void ASmartNPC::BeginPlay()
{
    Super::BeginPlay();

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->RegisterNPC(AgentID, this);
        }
    }

    AddStateTag(FGameplayTag::RequestGameplayTag(FName("State.Idle")));

    // TODO: Register event listeners
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

// [의도(Why)] 기존 직접 호출(ExecuteActionBatch)은 INPC 인터페이스 구현체로 통합합니다.
void ASmartNPC::ExecuteActionBatch(const FActionBatch& Batch)
{
    if (ActionComponent)
    {
        ActionComponent->ExecuteActionBatch(Batch);
    }
}



void ASmartNPC::SetBlackboardBool(const FString& KeyName, bool bValue)
{
    ASmartNPCAIController* AICtrl = Cast<ASmartNPCAIController>(GetController());
    if (!AICtrl) return;

    UBlackboardComponent* BlackboardComp = AICtrl->GetBlackboardComponent();
    if (!BlackboardComp) return;

    BlackboardComp->SetValueAsBool(FName(*KeyName), bValue);
    UE_LOG(LogTemp, Log, TEXT("[SmartNPC] Blackboard key '%s' set to %s on NPC '%s'"),
        *KeyName,
        bValue ? TEXT("true") : TEXT("false"),
        *AgentID);
}


void ASmartNPC::OnActionCompleted()
{
    if (ActionComponent)
    {
        ActionComponent->OnActionCompleted();
    }
}

float ASmartNPC::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, 
    AController* EventInstigator, AActor* DamageCauser)
{
    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    
    if (StateComponent)
    {
        StateComponent->ApplyDamage(ActualDamage);

        // [의도(Why)] 피격 정보를 인지 이벤트 배칭 시스템으로 전송하여 즉각적인 상황 인지 및 전략적 판단(도주, 반격 등)을 유도합니다.
        FPerceptionData DamageEventPerc;
        DamageEventPerc.TargetID = DamageCauser ? DamageCauser->GetName() : TEXT("Unknown");
        DamageEventPerc.SenseType = ESenseType::Hit; // 물리적 충격
        DamageEventPerc.Location = DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation();
        DamageEventPerc.Distance = DamageCauser ? FVector::Dist(GetActorLocation(), DamageEventPerc.Location) : 0.0f;
        DamageEventPerc.DangerScore = 1.0f; // 피격은 즉각적인 최대 위협으로 간주
        
        StateComponent->RequestEventCognition(DamageEventPerc);
    }

    return ActualDamage;
}

namespace
{
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
    FString ActionsJson = TEXT(R"({
                    "ActionType": "Attack",
                    "Parameters": {
                        "TargetID": "Player"
                    }
                })");
    DispatchDebugJson(this, TEXT("Combat"), ActionsJson);
}

void ASmartNPC::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    TagContainer = GameplayTags;
}

void ASmartNPC::AddStateTag(FGameplayTag Tag)
{
    if (Tag.IsValid())
    {
        GameplayTags.AddTag(Tag);
    }
}

void ASmartNPC::RemoveStateTag(FGameplayTag Tag)
{
    if (Tag.IsValid() && GameplayTags.HasTagExact(Tag))
    {
        GameplayTags.RemoveTag(Tag);
    }
}

bool ASmartNPC::IsHostileTo_Implementation(const TScriptInterface<ICharacterBase>& Other) const
{
    if (!StateComponent || !Other.GetObject()) return false;

    const FString OtherID = ICharacterBase::Execute_GetEntityID(Other.GetObject());
    // AffinityHostileThreshold 이하면 적대 관계
    return StateComponent->GetAffinityMultiplier(OtherID) >= 1.0f;
}

void ASmartNPC::Debug_PrintAffinity()
{
    if (!StateComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Affinity] %s: StateComponent 없음"), *AgentID);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("=== [Affinity] %s (Friendly>=%d, Hostile<=%d) ==="),
        *AgentID, StateComponent->AffinityFriendlyThreshold, StateComponent->AffinityHostileThreshold);

    if (StateComponent->AffinityCache.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("  (캐시 비어있음 — 아직 state_update를 받지 못함)"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow,
                FString::Printf(TEXT("[%s] Affinity: 캐시 없음"), *AgentID));
        }
        return;
    }

    for (const TPair<FString, int32>& Pair : StateComponent->AffinityCache)
    {
        const float Mult = StateComponent->GetAffinityMultiplier(Pair.Key);
        FString Relation = TEXT("Neutral");
        if (Pair.Value >= StateComponent->AffinityFriendlyThreshold) Relation = TEXT("Friendly");
        else if (Pair.Value <= StateComponent->AffinityHostileThreshold) Relation = TEXT("Hostile");

        UE_LOG(LogTemp, Warning, TEXT("  %s: score=%d (%s, multiplier=%.2f)"),
            *Pair.Key, Pair.Value, *Relation, Mult);

        if (GEngine)
        {
            FColor LineColor = FColor::White;
            if (Relation == TEXT("Friendly")) LineColor = FColor::Green;
            else if (Relation == TEXT("Hostile")) LineColor = FColor::Red;

            GEngine->AddOnScreenDebugMessage(-1, 8.f, LineColor,
                FString::Printf(TEXT("[%s→%s] %d (%s, mult=%.2f)"),
                    *AgentID, *Pair.Key, Pair.Value, *Relation, Mult));
        }
    }
}

void ASmartNPC::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bShowAffinityOnScreen || !StateComponent) return;

    UWorld* World = GetWorld();
    if (!World) return;

    // 머리 위 텍스트 오프셋
    const FVector TextOffset(0.f, 0.f, 110.f);
    const FVector BaseLoc = GetActorLocation() + TextOffset;

    if (StateComponent->AffinityCache.Num() == 0)
    {
        DrawDebugString(World, BaseLoc,
            FString::Printf(TEXT("[%s] Affinity: (none)"), *AgentID),
            nullptr, FColor::Yellow, 0.f, true, 0.9f);
        return;
    }

    int32 LineIdx = 0;
    for (const TPair<FString, int32>& Pair : StateComponent->AffinityCache)
    {
        FColor LineColor = FColor::White;
        if (Pair.Value >= StateComponent->AffinityFriendlyThreshold) LineColor = FColor::Green;
        else if (Pair.Value <= StateComponent->AffinityHostileThreshold) LineColor = FColor::Red;

        const FVector LineLoc = BaseLoc + FVector(0.f, 0.f, -15.f * LineIdx);
        DrawDebugString(World, LineLoc,
            FString::Printf(TEXT("%s: %d"), *Pair.Key, Pair.Value),
            nullptr, LineColor, 0.f, true, 0.9f);
        ++LineIdx;
    }
}
