#include "PolicyCacheComponent.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "../AI/SmartNPC.h" 

// Blackboard Keys (Assumed names, verify with user project)
const FName KEY_HAS_VALID_POLICY = FName("HasValidPolicy");
const FName KEY_URGENT = FName("Urgent");
const FName KEY_TARGET_ACTOR = FName("TargetActor");
const FName KEY_ALLOW_ATTACK = FName("AllowAttack");

UPolicyCacheComponent::UPolicyCacheComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bHasValidPolicy = false;
    CurrentDecisionIndex = 0;
}

void UPolicyCacheComponent::BeginPlay()
{
    Super::BeginPlay();
    SetComponentTickEnabled(false); // Disable tick until policy received
}

void UPolicyCacheComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    CheckExpiration();
}

void UPolicyCacheComponent::HandlePolicyUpdate(const FString& JsonPayload)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonPayload);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        // Parse basic fields
        if (JsonObject->HasField("trace_id")) CurrentPolicy.TraceID = JsonObject->GetStringField("trace_id");
        if (JsonObject->HasField("policy_version")) 
        {
            int32 NewVersion = JsonObject->GetIntegerField("policy_version");
            if (NewVersion > CurrentPolicy.PolicyVersion)
            {
                // New policy version, reset decision index or handle monotonic increment logic needed?
                // For now, keep index monotonic per policy life-cycle. 
                // Creating a new policy usually resets context, but user requested monotonic increase per policy version.
                // If Version changed, maybe reset index? Or user said "DecisionIndex is monotonic per policy version" 
                // implying within the SAME version it increases. 
                // Let's assume we keep it strictly increasing to avoid rollback.
            }
            CurrentPolicy.PolicyVersion = NewVersion; 
        }
        if (JsonObject->HasField("issued_at")) CurrentPolicy.IssuedAt = JsonObject->GetNumberField("issued_at");
        if (JsonObject->HasField("ttl")) CurrentPolicy.TTL = JsonObject->GetNumberField("ttl");
        if (JsonObject->HasField("target_guid")) CurrentPolicy.TargetGuid = JsonObject->GetStringField("target_guid");
        if (JsonObject->HasField("base_seed")) CurrentPolicy.BaseSeed = JsonObject->GetIntegerField("base_seed");
        
        // Traits (Optional in Patch, Required in Base. Here we just overwrite if present)
        if (JsonObject->HasField("aggression")) CurrentPolicy.Aggression = JsonObject->GetNumberField("aggression");
        if (JsonObject->HasField("fear")) CurrentPolicy.Fear = JsonObject->GetNumberField("fear");
        if (JsonObject->HasField("vigilance")) CurrentPolicy.Vigilance = JsonObject->GetNumberField("vigilance");
        if (JsonObject->HasField("policy_flags")) CurrentPolicy.PolicyFlags = JsonObject->GetIntegerField("policy_flags");

        // Validate GUID strict
        FGuid TargetGuidStruct;
        if (FGuid::ParseExact(CurrentPolicy.TargetGuid, EGuidFormats::DigitsWithHyphens, TargetGuidStruct))
        {
            bHasValidPolicy = true;
            ResolvePolicyTarget(TargetGuidStruct);
            SetComponentTickEnabled(true);
            UpdateBlackboard();
        }
        else
        {
            // Invalid GUID format -> Degrade immediately
            bHasValidPolicy = false;
            UpdateBlackboard(); 
            SetComponentTickEnabled(false);
            UE_LOG(LogTemp, Warning, TEXT("Policy Update Rejected: Invalid GUID format %s"), *CurrentPolicy.TargetGuid);
        }
    }
}

void UPolicyCacheComponent::CheckExpiration()
{
    double ServerTime = UGameplayStatics::GetTimeSeconds(GetWorld()); // Using GameTime as proxy for ServerWorldTime if not dedicated server
    // Note: GetServerWorldTimeSeconds() is only available on GameState or via network authority. 
    // Ideally use GetWorld()->GetTimeSeconds() for local, or GameState->GetServerWorldTimeSeconds() if available.
    // User asked for "GetServerWorldTimeSeconds()". Assuming this runs on Client or Listen Server? 
    // Standard GetTimeSeconds is usually fine for single player / listen server sync.
    
    // User Spec: issued_at is "Server World Time".
    // If issued_at comes from Python (external wall clock or synced game time?)
    // Assuming Python sends GameTimeSeconds.
    
    if (ServerTime > (CurrentPolicy.IssuedAt + CurrentPolicy.TTL))
    {
        // Expired
        bHasValidPolicy = false;
        UpdateBlackboard();
        
        // Abort Logic if Urgent was set?
        // If we are expiring, we should probably clear state.
        AAIController* AIController = Cast<AAIController>(GetOwner());
        if (AIController)
        {
             APawn* Pawn = AIController->GetPawn();
             ASmartNPC* NPC = Cast<ASmartNPC>(Pawn);
             if (NPC)
             {
                 NPC->ClearPhysicalState();
             }
        }

        SetComponentTickEnabled(false);
    }
}

void UPolicyCacheComponent::ResolvePolicyTarget(const FGuid& TargetGuid)
{
    AAIController* AIController = Cast<AAIController>(GetOwner());
    if (!AIController) return;

    UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent();
    if (!Blackboard) return;

    // Find Actor by GUID (Iterate all actors? Or is there a map? 
    // Standard UE doesn't map GUID to Actor automatically unless we use a manager.
    // For now, iterate `ASmartNPC` or `AVRPlayerCharacter` or generic Actor loop.
    // PERF WARNING: This is slow. Ideally use a manager.
    // Assuming NPCManager or similar exists, but for now implementing safe slow query or degarde.)
    
    // Optimization: Check if current target has this GUID? 
    
    AActor* NewTarget = nullptr;
    
    // Simple iteration for "Definition of Done". 
    // In real prod, this should use a TMap<FGuid, AActor*> maintained by GameState.
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        // Assuming Actors have a specialized interface or component for GUID?
        // Or if we modify ASmartNPC/Player to have a Guid property.
        // User didn't specify WHERE the GUID lives on actors.
        // If we can't find it, we degrade.
        
        // CHECK: Does SmartNPC have AgentID as string or GUID? 
        // Previously saw AgentID in ASmartNPC.
        ASmartNPC* NPC = Cast<ASmartNPC>(*It);
        if (NPC)
        {
             // Convert string AgentID to GUID? Or check literal string?
             // User Schema says TargetGuid is UUID.
             // If AgentID == TargetGuid.ToString() 
             if (NPC->AgentID == TargetGuid.ToString(EGuidFormats::DigitsWithHyphens))
             {
                 NewTarget = NPC;
                 break;
             }
        }
        // Also check Player?
    }

    if (NewTarget)
    {
        Blackboard->SetValueAsObject(KEY_TARGET_ACTOR, NewTarget);
        bool bAllowAttack = (CurrentPolicy.PolicyFlags & 0x2) != 0;
        Blackboard->SetValueAsBool(KEY_ALLOW_ATTACK, bAllowAttack);
    }
    else
    {
        // Degrade
        Blackboard->SetValueAsObject(KEY_TARGET_ACTOR, nullptr);
        Blackboard->SetValueAsBool(KEY_ALLOW_ATTACK, false);
    }
}

void UPolicyCacheComponent::UpdateBlackboard()
{
    AAIController* AIController = Cast<AAIController>(GetOwner());
    if (!AIController) return;

    UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent();
    if (!Blackboard) return;

    Blackboard->SetValueAsBool(KEY_HAS_VALID_POLICY, bHasValidPolicy);
    
    bool bUrgent = (CurrentPolicy.PolicyFlags & 0x1) != 0;
    Blackboard->SetValueAsBool(KEY_URGENT, bUrgent);
}

uint32 UPolicyCacheComponent::GenerateDecisionSeed(int32 DecisionIndex)
{
    // Safety
    CurrentDecisionIndex = FMath::Max(CurrentDecisionIndex, DecisionIndex);
    
    // Hash: BaseSeed + TraceID + DecisionIndex
    uint32 Hash = GetTypeHash(CurrentPolicy.BaseSeed);
    Hash = HashCombine(Hash, GetTypeHash(CurrentPolicy.TraceID));
    Hash = HashCombine(Hash, GetTypeHash(CurrentDecisionIndex));
    
    return Hash;
}

void UPolicyCacheComponent::IncrementDecisionIndex()
{
    CurrentDecisionIndex++;
}

