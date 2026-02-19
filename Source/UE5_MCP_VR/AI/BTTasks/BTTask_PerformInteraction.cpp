#include "BTTask_PerformInteraction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "../NPCActionKeys.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_PerformInteraction::UBTTask_PerformInteraction()
{
    NodeName = "Perform Interaction";
}

EBTNodeResult::Type UBTTask_PerformInteraction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    AAIController* AIController = OwnerComp.GetAIOwner();
    if (!BB || !AIController) return EBTNodeResult::Failed;

    ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
    if (!NPC) return EBTNodeResult::Failed;

    // 1. Get Interaction Key (from Behavior Mode or SubAction Key)
    // Actually, SmartNPC sets SubAction to the ActionType (e.g. "Sit", "Dance")
    FString InteractionKey = BB->GetValueAsString(ASmartNPCAIController::Key_SubAction);
    
    // 2. Get Target Actor (Physical Object)
    AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));

    // 3. Get Extra Parameters (JSON)
    FString ParamsJson = BB->GetValueAsString(ASmartNPCAIController::Key_ActionParameters);
    
    // Parse JSON to get TargetID and simple ExtraParams string
    FString TargetID = TEXT("");
    FString ExtraParams = TEXT(""); // Flattened params or specific key?
    
    // To support complex params, we pass the raw JSON, or extract a specific field.
    // Let's parse JSON to get specific fields first
    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJson);
    if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
    {
        // Extract TargetID if TargetActor is null
        if (!TargetActor)
        {
            TargetID = JsonObj->GetStringField(NPCActionKeys::Key_TargetID);
        }

        // For ExtraParams, we might pass the entire JSON or specific fields depending on context.
        // For general use, let's pass a specific 'style' or 'variant' field if present, 
        // or just pass the whole JSON if ExecuteInteraction supports parsing.
        // SmartNPC::ExecuteInteraction signature: (Type, Actor, TargetID, ExtraParams)
        // Let's extract 'style' or 'song' as ExtraParams for now, or just pass raw JSON if empty.
        
        if (JsonObj->HasField(TEXT("style")))
        {
            ExtraParams = JsonObj->GetStringField(TEXT("style"));
        }
        else if (JsonObj->HasField(TEXT("song")))
        {
             ExtraParams = JsonObj->GetStringField(TEXT("song"));
        }
        else
        {
            // Fallback: Pass raw JSON for complex parsing inside NPC
            // ExtraParams = ParamsJson; 
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[BTTask] Perfoming Interaction: %s (Target: %s, ID: %s)"), *InteractionKey, TargetActor ? *TargetActor->GetName() : TEXT("None"), *TargetID);

    // 4. Execute Interaction
    NPC->ExecuteInteraction(InteractionKey, TargetActor, TargetID, ExtraParams);

    // 5. Completion
    // SmartNPC usually expects an explicit FinishExecute.
    // However, this task is instantaneous execution (fire-and-forget logic for now).
    // The NPC internal state handles the duration/animation.
    
    // Wait logic: If the action is "Wait" or has duration, we might want to return InProgress.
    // But currently ExecuteInteraction launches a Montage.
    // To synchronize, we'd need an event from NPC when montage finishes.
    // For this refactor, we assume fire-and-forget or handled by a separate Wait task.
    // (Constraint: "Constraint 2: Just play animation for now")
    
    return EBTNodeResult::Succeeded;
}

FString UBTTask_PerformInteraction::GetStaticDescription() const
{
    return FString::Printf(TEXT("Executes Interaction defined in Blackboard SubAction Key.\nTarget: Key_TargetActor\nParams: Key_ActionParameters"));
}
