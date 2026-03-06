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
#include "Perception/UAIPerceptionStimuliSourceComponent.h"

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

// [의도(Why)] 기존 직접 호출(ExecuteActionBatch)은 INPCEntity 인터페이스 구현체로 통합합니다.
void ASmartNPC::ExecuteActionBatch_Implementation(const FActionBatch& Batch)
{
    if (ActionComponent)
    {
        ActionComponent->ExecuteActionBatch(Batch);
    }
}

float ASmartNPC::GetHealth_Implementation() const
{
    return StateComponent ? StateComponent->GetCurrentStats().Resources.Health : 0.f;
}

bool ASmartNPC::IsAlive_Implementation() const
{
    return StateComponent ? StateComponent->GetCurrentStats().Resources.IsAlive() : false;
}

namespace
{
    FString ExtractActivityContext(AActor* SensedActor)
    {
        if (IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(SensedActor))
        {
            FGameplayTagContainer TargetTags;
            TagInterface->GetOwnedGameplayTags(TargetTags);
            
            for (const FGameplayTag& Tag : TargetTags)
            {
                if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("State.Action"))))
                {
                    return Tag.GetTagName().ToString();
                }
            }
        }
        return TEXT("Idle");
    }

    bool TryIdentifySightTarget(AActor* SensedActor, float Distance, float BaseIdentifyThreshold, float CurrentTime, TMap<TWeakObjectPtr<AActor>, FKnownTargetInfo>& KnownTargetsMap, APawn*& OutIdentifiedPawn)
    {
        TWeakObjectPtr<AActor> WeakActor(SensedActor);
        bool bWasAlreadyIdentified = KnownTargetsMap.Contains(WeakActor);
        
        float ThresholdToUse = bWasAlreadyIdentified ? (BaseIdentifyThreshold + 200.f) : BaseIdentifyThreshold;

        if (Distance <= ThresholdToUse)
        {
            OutIdentifiedPawn = Cast<APawn>(SensedActor);
            if (OutIdentifiedPawn)
            {
                FKnownTargetInfo KnownInfo;
                KnownInfo.LastLocation = SensedActor->GetActorLocation();
                KnownInfo.LastSeenTime = CurrentTime;
                KnownTargetsMap.Add(WeakActor, KnownInfo);
                return true;
            }
        }
        return false;
    }

    bool TryIdentifyHearingTarget(AActor* SensedActor, const FAIStimulus& Stimulus, float HearingAssocRadius, float TargetTTL, float CurrentTime, TMap<TWeakObjectPtr<AActor>, FKnownTargetInfo>& KnownTargetsMap, APawn*& OutIdentifiedPawn)
    {
        TWeakObjectPtr<AActor> BestMatchActor = nullptr;
        float ClosestDistSq = FMath::Square(HearingAssocRadius);

        for (auto It = KnownTargetsMap.CreateIterator(); It; ++It)
        {
            TWeakObjectPtr<AActor> SavedActor = It.Key();
            FKnownTargetInfo& KnownInfo = It.Value();

            if (!SavedActor.IsValid())
            {
                It.RemoveCurrent();
                continue;
            }

            if (CurrentTime - KnownInfo.LastSeenTime > TargetTTL)
            {
                It.RemoveCurrent();
                continue;
            }

            float DistSq = FVector::DistSquared(Stimulus.StimulusLocation, KnownInfo.LastLocation);
            if (DistSq < ClosestDistSq)
            {
                ClosestDistSq = DistSq;
                BestMatchActor = SavedActor;
            }
        }

        if (BestMatchActor.IsValid())
        {
            OutIdentifiedPawn = Cast<APawn>(BestMatchActor.Get());
            return true;
        }
        
        if (Stimulus.Tag == FName("Voice")) 
        {
            OutIdentifiedPawn = Cast<APawn>(SensedActor);
            return OutIdentifiedPawn != nullptr;
        }

        return false;
    }

    bool TryProcessStimulus(
        AActor* SensedActor, const FAIStimulus& Stimulus, float CurrentTime, 
        float IdentifyRadius, float HearingRadius, float MemoryTTL,
        TMap<TWeakObjectPtr<AActor>, FKnownTargetInfo>& KnownTargetsMap,
        const FVector& OwnerLocation,
        FPerceptionData& OutPercData)
    {
        OutPercData.Location = SensedActor->GetActorLocation();
        OutPercData.Distance = FVector::Dist(OwnerLocation, OutPercData.Location);

        FAISenseID SightID = UAISense::GetSenseID<UAISense_Sight>();
        FAISenseID HearingID = UAISense::GetSenseID<UAISense_Hearing>();

        bool bIsIdentified = false;
        APawn* IdentifiedPawn = nullptr;

        if (Stimulus.Type == SightID)
        {
            float CurrentLightFactor = 1.0f; 
            float DynamicThreshold = IdentifyRadius * CurrentLightFactor;
            bIsIdentified = TryIdentifySightTarget(SensedActor, OutPercData.Distance, DynamicThreshold, CurrentTime, KnownTargetsMap, IdentifiedPawn);
            OutPercData.SenseType = ESenseType::Sight;
        }
        else if (Stimulus.Type == HearingID)
        {
            bIsIdentified = TryIdentifyHearingTarget(SensedActor, Stimulus, HearingRadius, MemoryTTL, CurrentTime, KnownTargetsMap, IdentifiedPawn);
            OutPercData.SenseType = ESenseType::Hearing;
        }
        else
        {
            OutPercData.SenseType = ESenseType::Other;
        }

        OutPercData.TargetID = (bIsIdentified && IdentifiedPawn) ? IdentifiedPawn->GetName() : TEXT("unknown");
        OutPercData.ActivityContext = ExtractActivityContext(SensedActor);

        return true;
    }

    void CollectPerceptionData(
        ASmartNPC* NPC, 
        TArray<FPerceptionData>& OutPerceivedTargets)
    {
        ASmartNPCAIController* AIController = Cast<ASmartNPCAIController>(NPC->GetController());
        if (!AIController) return;
        
        UAIPerceptionComponent* PerceptionComp = AIController->GetAIPerceptionComponent();
        if (!PerceptionComp) return;

        TArray<AActor*> SensedActors;
        PerceptionComp->GetKnownPerceivedActors(nullptr, SensedActors);
        
        float CurrentTime = NPC->GetWorld()->GetTimeSeconds();

        for (AActor* SensedActor : SensedActors)
        {
            if (!IsValid(SensedActor) || SensedActor == NPC) continue;

            FActorPerceptionBlueprintInfo Info;
            if (!PerceptionComp->GetActorsPerception(SensedActor, Info)) continue;

            for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
            {
                if (!Stimulus.WasSuccessfullySensed()) continue;

                FPerceptionData PercData;
                if (TryProcessStimulus(SensedActor, Stimulus, CurrentTime, NPC->BaseIdentificationRadius, NPC->HearingAssociationRadius, NPC->TargetMemoryTTL, NPC->KnownTargetsMap, NPC->GetActorLocation(), PercData))
                {
                    OutPerceivedTargets.Add(PercData);
                    break;
                }
            }
        }
    }

    FString DetermineThreatLevel(UNPCStateComponent* StateComponent)
    {
        if (!StateComponent) return TEXT("Low");

        const FCharacterAttributes CurrentStats = StateComponent->GetCurrentStats();
        const float HealthRatio = CurrentStats.Resources.Health / FMath::Max(1.f, CurrentStats.Resources.MaxHealth);
        
        if (HealthRatio < 0.3f) return TEXT("High");
        if (HealthRatio < 0.6f) return TEXT("Medium");
        return TEXT("Low");
    }
}

void ASmartNPC::CollectAndSendStateUpdate()
{
    if (!IsValid(this) || AgentID.IsEmpty()) return;
    
    FGameStateData StateSnapshot;
    StateSnapshot.OwnerAgentID = AgentID;
    StateSnapshot.OwnerLocation = GetActorLocation();
    StateSnapshot.ThreatLevel = DetermineThreatLevel(StateComponent);

    CollectPerceptionData(this, StateSnapshot.PerceivedTargets);

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UNPCManager* Manager = GI->GetSubsystem<UNPCManager>())
        {
            Manager->SendStateToMCP(StateSnapshot);
        }
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
    return StateComponent ? StateComponent->GetCurrentStats() : FCharacterAttributes();
}

float ASmartNPC::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, 
    AController* EventInstigator, AActor* DamageCauser)
{
    float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    
    if (StateComponent && GetWorld()->GetTimeSeconds() - StateComponent->LastHitTime > 2.0f)
    {
        StateComponent->ApplyDamage(ActualDamage);
        StateComponent->RequestEmergencyCognition(TEXT("Hit"), 
            FString::Printf(TEXT("Took %.1f Damage"), ActualDamage));
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
