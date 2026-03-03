#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"

// Define Key Names
const FName ASmartNPCAIController::Key_TargetLocation(TEXT("TargetLocation"));

const FName ASmartNPCAIController::Key_TargetActor(TEXT("TargetActor"));

const FName ASmartNPCAIController::Key_BehaviorMode(TEXT("BehaviorMode"));
const FName ASmartNPCAIController::Key_HasAction(TEXT("HasAction"));
const FName ASmartNPCAIController::Key_SubAction(TEXT("SubAction"));
const FName ASmartNPCAIController::Key_Parameters(TEXT("Parameters"));
const FName ASmartNPCAIController::Key_FacialState(TEXT("FacialState"));

ASmartNPCAIController::ASmartNPCAIController()
{
    // Initialize AI Perception
    PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerceptionComponent"));
    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));

    if (PerceptionComp && SightConfig && HearingConfig)
    {
        // 1. Configure Sight Sense
        SightConfig->SightRadius = 3000.0f;
        SightConfig->LoseSightRadius = 3500.0f;
        SightConfig->PeripheralVisionAngleDegrees = 60.0f;
        SightConfig->SetMaxAge(5.0f);
        SightConfig->DetectionByAffiliation.bDetectEnemies = true;
        SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
        SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

        // 2. Configure Hearing Sense
        HearingConfig->HearingRange = 3000.0f;
        HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
        HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
        HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

        // 3. Register configurations
        PerceptionComp->ConfigureSense(*SightConfig);
        PerceptionComp->ConfigureSense(*HearingConfig);
        PerceptionComp->SetDominantSense(SightConfig->GetSenseImplementation());

        // 4. Bind Event
        PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &ASmartNPCAIController::OnTargetPerceptionUpdated);
    }
}

void ASmartNPCAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (ASmartNPC* NPC = Cast<ASmartNPC>(InPawn))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SmartNPCAIController] Possessed NPC: %s"), *NPC->AgentID);
		
		// --- Apply NPC-specific Vision Config ---
		if (PerceptionComp && SightConfig)
		{
			SightConfig->SightRadius = NPC->SightRadius;
			SightConfig->LoseSightRadius = NPC->LoseSightRadius;
			SightConfig->PeripheralVisionAngleDegrees = NPC->SightAngle;
			
			// Re-configure sense with new values from NPC
			PerceptionComp->ConfigureSense(*SightConfig);

			if (HearingConfig)
			{
				HearingConfig->HearingRange = NPC->HearingRange;
				PerceptionComp->ConfigureSense(*HearingConfig);
			}

			PerceptionComp->SetDominantSense(SightConfig->GetSenseImplementation());
			
			UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] Applied Vision/Hearing Config for %s: Sight=%.1f, Hearing=%.1f"), 
				*NPC->AgentID, NPC->SightRadius, NPC->HearingRange);
		}

		if (NPC->BehaviorTreeAsset && NPC->BehaviorTreeAsset->BlackboardAsset)
		{
			RunBehaviorTree(NPC->BehaviorTreeAsset);
		}
	}
}

void ASmartNPCAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (!Blackboard) return;

    // Check if we just saw/heard something
    if (Stimulus.WasSuccessfullySensed())
    {
        // 1. Identify which sense triggered this
        FAISenseID SightID = UAISense::GetSenseID<UAISense_Sight>();
        FAISenseID HearingID = UAISense::GetSenseID<UAISense_Hearing>();

        if (Stimulus.Type == SightID)
        {
            UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] SIGHT: Detected %s"), *Actor->GetName());
            // Found target via SIGHT (Exact location known)
            Blackboard->SetValueAsObject(Key_TargetActor, Actor);
            Blackboard->SetValueAsVector(Key_TargetLocation, Actor->GetActorLocation());
        }
        else if (Stimulus.Type == HearingID)
        {
            // 자신이 발생시킨 소음(ReportNoiseEvent의 Instigator=self)인 경우 자가 피드백 무시
            if (Actor == GetPawn())
            {
                return;
            }

            FString SourceName = Actor ? Actor->GetName() : TEXT("Unknown");
            UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] HEARING: Detected Noise from %s at %s"), *SourceName, *Stimulus.StimulusLocation.ToString());
            
            // Heard something! 
            // 시각 정보가 아니므로 TargetActor를 즉각 설정하지는 않지만, 소음 발생 위치를 조사(Investigate)의 목적으로 유지합니다.
            Blackboard->SetValueAsVector(Key_TargetLocation, Stimulus.StimulusLocation);

            // [Blueprint 반영]: 
            // 현재 NPC의 상태는 중앙 집중형 LLM이 일괄 제어하므로, 소리를 듣는 순간 즉시 행동을 덮어씌울 필요는 없습니다.
            // 대신 현재 의심 위치(TargetLocation)가 다음 StateUpdate 스냅샷 등을 통해 서버로 전달되면, 
            // "어디선가 소리가 났음"을 바탕으로 EQS 좌표 쿼리가 동작하여 LLM에 최종 반영됩니다.
        }
    }
    else
    {
        // Target lost or Sound ended
        AActor* CurrentTarget = Cast<AActor>(Blackboard->GetValueAsObject(Key_TargetActor));
        if (CurrentTarget == Actor)
        {
            UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] Lost target: %s"), *Actor->GetName());
            Blackboard->ClearValue(Key_TargetActor);
        }
    }
}
