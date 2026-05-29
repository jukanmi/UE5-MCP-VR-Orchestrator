#include "SmartNPCAIController.h"
#include "NPCActionComponent.h"
#include "../NPCStateComponent.h"
#include "../SmartNPC.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "MCPStateTreeAIComponent.h"
#include "StateTree.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"

// Define Key Names
const FName ASmartNPCAIController::Key_TargetLocation(TEXT("TargetLocation"));

const FName ASmartNPCAIController::Key_TargetActor(TEXT("TargetActor"));

namespace
{
    // 시각 발각 기본 위협도(직접 목격). 최종 danger = SightBaseDanger × 호감도배율.
    constexpr float SightBaseDanger = 0.6f;
    // 이 이상이면 EQS 전술 쿼리 발동(적대 판정). 중립(배율 0.5→0.3)은 미발동.
    constexpr float CombatDangerThreshold = 0.5f;
}


ASmartNPCAIController::ASmartNPCAIController()
{
    PerceptionTickInterval = 9.0f;

    // StateTree AI Component (SmartNPC.StateTreeAsset 설정 시 자동 실행)
    StateTreeAI = CreateDefaultSubobject<UMCPStateTreeAIComponent>(TEXT("StateTreeAI"));
    if (StateTreeAI)
    {
        // OnPossess에서 에셋 주입 후 수동으로 StartLogic — BeginPlay 자동 시작 비활성.
        StateTreeAI->SetStartLogicAutomatically(false);
    }

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
		UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] Possessed NPC: %s"), *NPC->AgentID);
		
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

		// --- Blackboard 초기화 ---
		// Perception 콜백과 ST Tasks가 BB를 통해 TargetActor 등을 공유.
		if (NPC->BlackboardAsset)
		{
			UBlackboardComponent* BBComp = nullptr;
			UseBlackboard(NPC->BlackboardAsset, BBComp);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SmartNPCAIController] %s: BlackboardAsset 미할당 — Perception 키 공유 불가"), *NPC->AgentID);
		}

		// --- StateTree 실행 ---
		if (NPC->StateTreeAsset && StateTreeAI)
		{
			// 핵심: StateTreeRef는 protected라 서브클래스 setter로 주입.
			// StartLogic() 전에 반드시 호출.
			StateTreeAI->SetStateTreeAsset(NPC->StateTreeAsset);
			StateTreeAI->StartLogic();
			UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] %s: StateTree logic started (asset=%s)"),
				*NPC->AgentID, *NPC->StateTreeAsset->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SmartNPCAIController] %s: StateTreeAsset 미할당 — AI 로직 실행 안됨"), *NPC->AgentID);
		}

        // --- Bind ActionComponent Delegates ---
        if (UNPCActionComponent* ActionComp = NPC->ActionComponent)
        {
            ActionComp->OnActionStarted.AddDynamic(this, &ASmartNPCAIController::HandleActionStarted);
            ActionComp->OnActionStoppedAll.AddDynamic(this, &ASmartNPCAIController::HandleAllActionsStopped);
        }
	}
}

void ASmartNPCAIController::OnUnPossess()
{
    // StateTree는 자체 정리하지만 명시적으로 멈추기. (사망/파괴 시 Tick 안전)
    if (StateTreeAI)
    {
        StateTreeAI->StopLogic(TEXT("UnPossess"));
    }
    Super::OnUnPossess();
}

void ASmartNPCAIController::HandleActionStarted(const FGameAction& /*Action*/)
{
}

void ASmartNPCAIController::HandleAllActionsStopped()
{
    // Key_TargetActor 해제 — STTask_PrepareNextAction의 자율 행동 주입 중단
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        BB->ClearValue(Key_TargetActor);
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
            const FString TargetID = Actor->GetName();
            UE_LOG(LogTemp, Verbose, TEXT("[SmartNPCAIController] SIGHT: Detected %s"), *TargetID);

            // 1. Blackboard 업데이트 (BehaviorTree용 즉각 반응)
            Blackboard->SetValueAsObject(Key_TargetActor, Actor);
            Blackboard->SetValueAsVector(Key_TargetLocation, Actor->GetActorLocation());

            // 2. FPerceptionData 조립 후 EventCognition으로 넘김
            if (ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetPawn()))
            {
                UNPCStateComponent* StateComp = OwnerNPC->StateComponent;
                if (StateComp)
                {
                    // 호감도 기반 최종 위협도 1회 계산 — RequestEventCognition + EQS 게이트 공유.
                    const float FinalDanger = StateComp->ComputePerceptionDanger(SightBaseDanger, TargetID);

                    FPerceptionData Perception;
                    Perception.TargetID = TargetID;
                    Perception.SenseType = ESenseType::Sight;
                    Perception.Location = Actor->GetActorLocation(); // 정확한 위치
                    Perception.Distance = FVector::Dist(OwnerNPC->GetActorLocation(), Actor->GetActorLocation());
                    Perception.DangerScore = FinalDanger;

                    StateComp->RequestEventCognition(Perception);

                    // Option B: 적대 위협(FinalDanger >= CombatDangerThreshold)일 때만 EQS 전술 쿼리.
                    // 중립(배율 0.5→danger 0.3) 감지 시 전투 포지셔닝 방지. (쿨다운 내장)
                    if (FinalDanger >= CombatDangerThreshold)
                    {
                        if (UNPCActionComponent* ActionComp = OwnerNPC->GetActionComponent())
                        {
                            TArray<FVector> EnemyLocs;
                            EnemyLocs.Add(Actor->GetActorLocation());
                            ActionComp->TryStartTacticalQueryForCombat(EnemyLocs);
                        }
                    }
                }
            }

            // 시야 대상이 바뀌었거나 타이머가 없을 때만 (재)시작
            if (CurrentSightTarget != Actor)
            {
                CurrentSightTarget = Actor;
                if (UWorld* W = GetWorld())
                {
                    W->GetTimerManager().SetTimer(
                        PerceptionTickTimer,
                        this, &ASmartNPCAIController::OnPerceptionTick,
                        PerceptionTickInterval,
                        true  // 반복
                    );
                }
            }
        }
        else if (Stimulus.Type == HearingID)
        {
            // 자신이 발생시킨 소음(ReportNoiseEvent의 Instigator=self)인 경우 자가 피드백 무시
            if (Actor == GetPawn())
            {
                return;
            }

            // 1. Tag 디코딩 (형식: "EventType:BaseDanger")
            FString TagStr = Stimulus.Tag.ToString();
            FString EventType, DangerStr;
            float BaseDanger = 0.2f; // 기본값

            if (TagStr.Split(TEXT(":"), &EventType, &DangerStr))
            {
                BaseDanger = FCString::Atof(*DangerStr);
            }
            else
            {
                EventType = TagStr; // Tag가 없으면 원본 그대로 EventType으로 사용
            }

            FString SourceName = Actor ? Actor->GetName() : TEXT("Unknown");
            UE_LOG(LogTemp, Verbose, TEXT("[SmartNPCAIController] HEARING: Detected %s Noise from %s at %s"),
                *EventType, *SourceName, *Stimulus.StimulusLocation.ToString());
            
            // Heard something! 
            // 시각 정보가 아니므로 TargetActor를 즉각 설정하지는 않지만, 소음 발생 위치를 조사(Investigate)의 목적으로 유지합니다.
            Blackboard->SetValueAsVector(Key_TargetLocation, Stimulus.StimulusLocation);

            // 2. 호감도(Affinity) 기반 가중치 계산
            if (ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetPawn()))
            {
                if (UNPCStateComponent* StateComp = OwnerNPC->StateComponent)
                {
                    // 호감도 기반 위협도 (Friend: 0.0, Neutral: 0.5×, Enemy: 1.0×)
                    const float FinalDanger = StateComp->ComputePerceptionDanger(BaseDanger, SourceName);

                    // 3. FPerceptionData 조립 후 EventCognition으로 넘김
                    FPerceptionData Perception;
                    Perception.TargetID = SourceName;
                    Perception.SenseType = ESenseType::Hearing;
                    Perception.Location = Stimulus.StimulusLocation; // [FIX] 소음 발생 위치 추가
                    Perception.Distance = FVector::Dist(OwnerNPC->GetActorLocation(), Stimulus.StimulusLocation);
                    Perception.DangerScore = FinalDanger;

                    StateComp->RequestEventCognition(Perception);

                    // 전투 관련 EventType(Attack/Damage/Hit) 또는 높은 원본 위험도면 EQS 전술 쿼리 트리거.
                    // FinalDanger가 아닌 BaseDanger 사용 — 호감도 미캐싱 시 Multiplier가 0.5로 떨어져 Attack도 차단되는 문제 회피.
                    const bool bIsCombatNoise =
                        EventType.Contains(TEXT("Attack")) ||
                        EventType.Contains(TEXT("Damage")) ||
                        EventType.Contains(TEXT("Hit")) ||
                        BaseDanger >= 0.5f;

                    if (bIsCombatNoise)
                    {
                        if (UNPCActionComponent* ActionComp = OwnerNPC->GetActionComponent())
                        {
                            TArray<FVector> EnemyLocs;
                            EnemyLocs.Add(Stimulus.StimulusLocation);
                            ActionComp->TryStartTacticalQueryForCombat(EnemyLocs);
                        }
                    }
                }
            }
        }
    }
    else
    {
        // Target lost or Sound ended
        AActor* CurrentTarget = Cast<AActor>(Blackboard->GetValueAsObject(Key_TargetActor));
        if (CurrentTarget == Actor)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[SmartNPCAIController] Lost target: %s"), *Actor->GetName());
            Blackboard->ClearValue(Key_TargetActor);
        }

        // 소실된 대상이 주기적 감시 대상이면 타이머 해제
        if (CurrentSightTarget == Actor)
        {
            if (UWorld* W = GetWorld())
                W->GetTimerManager().ClearTimer(PerceptionTickTimer);
            CurrentSightTarget.Reset();
        }
    }
}

void ASmartNPCAIController::OnPerceptionTick()
{
    AActor* Target = CurrentSightTarget.Get();
    if (!Target)
    {
        if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(PerceptionTickTimer);
        return;
    }

    ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetPawn());
    if (!OwnerNPC) return;

    UNPCStateComponent* StateComp = OwnerNPC->StateComponent;
    if (!StateComp) return;

    const FString TargetID = Target->GetName();

    FPerceptionData Perception;
    Perception.TargetID = TargetID;
    Perception.SenseType = ESenseType::Sight;
    Perception.Location = Target->GetActorLocation();
    Perception.Distance = FVector::Dist(OwnerNPC->GetActorLocation(), Target->GetActorLocation());
    Perception.DangerScore = StateComp->ComputePerceptionDanger(SightBaseDanger, TargetID);

    UE_LOG(LogTemp, Verbose, TEXT("[SmartNPCAIController] PerceptionTick: %s dist=%.0f danger=%.2f"),
        *TargetID, Perception.Distance, Perception.DangerScore);

    StateComp->RequestEventCognition(Perception);
}
