#include "NPC/Action/SmartNPCAIController.h"
#include "NPC/Action/NPCActionComponent.h"
#include "NPC/Components/NPCStateComponent.h"
#include "NPC/BP/SmartNPC.h"
#include "Core/Types/PlayerGameplayTags.h"   // TAG_State_Condition_Dead (플레이어 사망 판정)
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "NPC/Action/MCPStateTreeAIComponent.h"
#include "StateTree.h"
#include "GameplayTagAssetInterface.h"
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

void ASmartNPCAIController::PauseAI()
{
    // StateTree 정지 — 진행 task 의 ExitState 호출. 넉다운 동안 새 액션 주입 차단.
    if (StateTreeAI)
    {
        StateTreeAI->StopLogic(TEXT("Knockdown"));
    }
    // 진행 중 MoveTo 등 이동 즉시 중단(래그돌과 위치 다툼 방지).
    StopMovement();
}

void ASmartNPCAIController::ResumeAI()
{
    // 에셋·Blackboard 는 OnPossess 에서 주입된 채 유지 → StartLogic 만으로 루트부터 재가동.
    if (StateTreeAI && GetPawn())
    {
        StateTreeAI->StartLogic();
    }
}

bool ASmartNPCAIController::IsTargetDead(const AActor* Target)
{
    if (!Target) return false;

    // NPC — HandleDeath 가 세우는 플래그. Destroy 지연(3초) 동안에도 즉시 사망 판정.
    if (const ASmartNPC* TargetNPC = Cast<ASmartNPC>(Target))
    {
        return TargetNPC->bIsDead;
    }

    // 플레이어(VRPawn) — PawnDeathUtils::HandleDeath 가 부여하는 사망 태그.
    if (const IGameplayTagAssetInterface* TagOwner = Cast<IGameplayTagAssetInterface>(Target))
    {
        return TagOwner->HasMatchingGameplayTag(TAG_State_Condition_Dead);
    }

    return false;
}

void ASmartNPCAIController::HandleCombatTargetDead(AActor* DeadTarget)
{
    ASmartNPC* NPC = Cast<ASmartNPC>(GetPawn());
    if (!NPC) return;

    UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] %s: 전투 타겟 '%s' 사망 — 전투 해제, Common 복귀"),
        *NPC->AgentID, DeadTarget ? *DeadTarget->GetName() : TEXT("Unknown"));

    // 진행 중 스윙·이동 즉시 중단(몽타주 포함) 후 잔여 큐 폐기.
    // StopAllActions 의 OnActionStoppedAll → HandleAllActionsStopped 가 BB.TargetActor 를 클리어한다.
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        ActionComp->AbortCurrentAction();
        ActionComp->StopAllActions();
        // 전투 종료로 진행 중 전술 쿼리는 무의미 — Idle 복귀(WaitingLLM 잔존 시 후속 보고 지연 방지).
        ActionComp->AbortTacticalQuery();
    }

    if (UNPCStateComponent* StateComp = NPC->StateComponent)
    {
        StateComp->SetBehaviorMode(ENPCBehaviorMode::Common);
        // replan 플래그 — 다음 상호작용 prompt 에서 강제 재계획.
        StateComp->FlagDangerReplan();
        // Phase 2 통보: 승리 사실을 Python 에 즉시 보고(메모리 기록용, 무행동 응답).
        StateComp->ReportCombatVictory(DeadTarget ? DeadTarget->GetName() : TEXT("Unknown"));
    }

    // ActionComp 부재 등으로 브로드캐스트가 못 지웠을 경우 대비 보강 클리어(BB 쓰기는 컨트롤러 소유).
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        BB->ClearValue(Key_TargetActor);
    }

    // 사망 대상 주기 감시 해제 — 시체 대상 perception 재보고 방지.
    if (CurrentSightTarget.Get() == DeadTarget)
    {
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().ClearTimer(PerceptionTickTimer);
        }
        CurrentSightTarget.Reset();
    }

    // 전투가 사망으로 이미 해제됨 — 대기 중이던 소실 타임아웃 취소.
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(CombatTargetLostTimer);
    }
}

void ASmartNPCAIController::HandleCombatTargetLostTimeout()
{
    ASmartNPC* NPC = Cast<ASmartNPC>(GetPawn());
    if (!NPC) return;

    UNPCStateComponent* StateComp = NPC->StateComponent;
    if (!StateComp || StateComp->GetBehaviorMode() != ENPCBehaviorMode::Combat) return;

    // 타이머 취소 누락 대비 이중 가드 — 그 사이 재발견(BB 타겟 유효)이면 전투 유지.
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        if (BB->GetValueAsObject(Key_TargetActor)) return;
    }

    UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] %s: 전투 타겟 장기 소실(%.1f초) — 전투 해제, Common 복귀"),
        *NPC->AgentID, CombatTargetLostTimeout);

    // HandleCombatTargetDead 와 동일 시퀀스 — 승리 보고(ReportCombatVictory)만 제외.
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        ActionComp->AbortCurrentAction();
        ActionComp->StopAllActions();
        ActionComp->AbortTacticalQuery();
    }

    StateComp->SetBehaviorMode(ENPCBehaviorMode::Common);
    StateComp->FlagDangerReplan(); // 재조우 시 'Combat 첫 진입' replan 경로 복원
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

            // 시야 재획득 — 진행 중이던 전투 소실 타임아웃 취소(짧은 엄폐는 전투 유지)
            if (UWorld* W = GetWorld())
            {
                W->GetTimerManager().ClearTimer(CombatTargetLostTimer);
            }

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

                    // 척수반사 — Python 왕복 없이 즉시 반응(SPEC_reflex_table).
                    // danger 게이트 **밖**에서 부른다: 친화 인사처럼 게이트를 못 넘는 자극이
                    // 반사의 주 대상이기 때문. 관계·거리 판정은 룰이 직접 한다.
                    if (UNPCActionComponent* ActionComp = OwnerNPC->GetActionComponent())
                    {
                        ActionComp->TryReflexReact(ESenseType::Sight, FString(), TargetID,
                            SightBaseDanger, Perception.Distance, Actor->GetActorLocation());
                    }

                    // 적대 위협(FinalDanger >= CombatDangerThreshold)일 때만 emergency report·EQS.
                    // 중립/친화(배율로 danger 하락) 감지 시 SLM 반사·전투 포지셔닝 모두 생략.
                    // 플레이어 상호작용은 dialogue(prompt) 경로로 처리.
                    if (FinalDanger >= CombatDangerThreshold)
                    {
                        StateComp->RequestEventCognition(Perception);

                        // Combat 첫 진입 시만 재계획 표시 — 이미 Combat 중이면 기존 plan 유지.
                        if (StateComp->GetBehaviorMode() != ENPCBehaviorMode::Combat)
                        {
                            StateComp->FlagDangerReplan();
                        }

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

                    if (UNPCActionComponent* ActionComp = OwnerNPC->GetActionComponent())
                    {
                        if (bIsCombatNoise)
                        {
                            TArray<FVector> EnemyLocs;
                            EnemyLocs.Add(Stimulus.StimulusLocation);
                            ActionComp->TryStartTacticalQueryForCombat(EnemyLocs);
                        }
                        else
                        {
                            // 전투 소음은 위 EQS 엄폐가 전담한다. 반사까지 끼면 즉시형 Scan 이
                            // 큐 앞을 막아 엄폐 이동이 밀리므로, 반사는 비전투 소음만 받는다.
                            ActionComp->TryReflexReact(ESenseType::Hearing, EventType, SourceName,
                                BaseDanger, Perception.Distance, Stimulus.StimulusLocation);
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

            // Combat 중 타겟 소실 — 타임아웃까지 재발견 없으면 전투 해제(잔존 Combat 조각상화 방지).
            if (ASmartNPC* NPC = Cast<ASmartNPC>(GetPawn()))
            {
                if (NPC->StateComponent && NPC->StateComponent->GetBehaviorMode() == ENPCBehaviorMode::Combat)
                {
                    if (UWorld* W = GetWorld())
                    {
                        W->GetTimerManager().SetTimer(CombatTargetLostTimer, this,
                            &ASmartNPCAIController::HandleCombatTargetLostTimeout, CombatTargetLostTimeout, false);
                    }
                }
            }
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

    // 저위협(친화적/중립) 대상은 emergency report 생략 — 반복 perception 마다 SLM 반사를
    // 때리지 않게. danger 가 임계 이상으로 오르면(호감도 하락 등) 그때 보고됨.
    if (Perception.DangerScore >= CombatDangerThreshold)
    {
        StateComp->RequestEventCognition(Perception);
        // Combat 첫 진입 시만 재계획 — 이미 Combat 중 지속 tick 은 무시(plan 폭주 방지).
        if (StateComp->GetBehaviorMode() != ENPCBehaviorMode::Combat)
        {
            StateComp->FlagDangerReplan();
        }
    }
}
