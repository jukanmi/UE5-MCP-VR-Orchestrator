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
#include "NPC/Subsystems/NPCManager.h"
#include "Network/EnvelopeBuilder.h"
#include "Dom/JsonObject.h"

// Define Key Names
const FName ASmartNPCAIController::Key_TargetLocation(TEXT("TargetLocation"));

const FName ASmartNPCAIController::Key_TargetActor(TEXT("TargetActor"));

namespace
{
    // 시각 발각 기본 위협도(직접 목격). 최종 danger = SightBaseDanger × 호감도배율.
    constexpr float SightBaseDanger = 0.6f;
    // 이 이상이면 EQS 전술 쿼리 발동(적대 판정). 중립(배율 0.5→0.3)은 미발동.
    constexpr float CombatDangerThreshold = 0.5f;

    // Python jev_decision stance 문자열 → enum. 미매칭은 Default(중립).
    EJevTacticalStance ParseJevStance(const FString& S)
    {
        if (S.Equals(TEXT("Aggressive"), ESearchCase::IgnoreCase)) return EJevTacticalStance::Aggressive;
        if (S.Equals(TEXT("Defensive"),  ESearchCase::IgnoreCase)) return EJevTacticalStance::Defensive;
        if (S.Equals(TEXT("Flee"),       ESearchCase::IgnoreCase)) return EJevTacticalStance::Flee;
        return EJevTacticalStance::Default;
    }
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

void ASmartNPCAIController::UpdateEQSBlackboardParams(float SearchRadius, float CoverWeight,
    float DistanceWeight, float AggressionWeight, float SafeDistance)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB) return;

    BB->SetValueAsFloat(FName("EQS_SearchRadius"),     SearchRadius);
    BB->SetValueAsFloat(FName("EQS_CoverWeight"),      CoverWeight);
    BB->SetValueAsFloat(FName("EQS_DistanceWeight"),   DistanceWeight);
    BB->SetValueAsFloat(FName("EQS_AggressionWeight"), AggressionWeight);
    BB->SetValueAsFloat(FName("EQS_SafeDistance"),     SafeDistance);
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
    // 전투 캐릭터(SmartNPC·EnemyCharacter)는 bIsDead, 플레이어는 사망 태그 — 판정은 베이스 한 곳.
    return ACombatCharacter::IsActorDead(Target);
}

void ASmartNPCAIController::ExitCombat(AActor* DeadTarget)
{
    ASmartNPC* NPC = Cast<ASmartNPC>(GetPawn());
    if (!NPC) return;

    if (DeadTarget)
    {
        UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] %s: 전투 타겟 '%s' 사망 — 전투 해제, Common 복귀"),
            *NPC->AgentID, *DeadTarget->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[SmartNPCAIController] %s: 전투 타겟 장기 소실(%.1f초) — 전투 해제, Common 복귀"),
            *NPC->AgentID, CombatTargetLostTimeout);
    }

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
        // replan 플래그 — 다음 상호작용 prompt 에서 강제 재계획(재조우 시 'Combat 첫 진입' 경로 복원).
        StateComp->FlagDangerReplan();
        // Phase 2 통보: 승리 사실을 Python 에 즉시 보고(메모리 기록용, 무행동 응답). 소실은 승리가 아니다.
        // 스토리 boss_killed 는 AgentID 와 exact match — 액터 이름(BP_..._C_0)이 아니라 AgentID 를 보낸다.
        if (DeadTarget) StateComp->ReportCombatVictory(ASmartNPC::PerceptionIdFor(DeadTarget));
    }

    // ActionComp 부재 등으로 브로드캐스트가 못 지웠을 경우 대비 보강 클리어(BB 쓰기는 컨트롤러 소유).
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        BB->ClearValue(Key_TargetActor);
    }

    // 사망 대상 주기 감시 해제 — 시체 대상 perception 재보고 방지.
    if (DeadTarget && CurrentSightTarget.Get() == DeadTarget)
    {
        StopSightTracking();
    }

    // 전투가 이미 해제됨 — 대기 중이던 소실 타임아웃 취소.
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(CombatTargetLostTimer);
    }
}

void ASmartNPCAIController::StopSightTracking()
{
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(PerceptionTickTimer);
    }
    CurrentSightTarget.Reset();
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

    ExitCombat(nullptr);
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
            const FString TargetID = ASmartNPC::PerceptionIdFor(Actor);
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

                    const FPerceptionData Perception(TargetID, ESenseType::Sight, Actor->GetActorLocation(),
                                                     OwnerNPC->GetActorLocation(), FinalDanger);

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

                        // 적대 감지는 jevlike 전술 편향 요청 트리거(쿨다운·in-flight 가드는 내부).
                        RequestJevDecision();
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

            FString SourceName = ASmartNPC::PerceptionIdFor(Actor);
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

                    // 3. FPerceptionData 조립 후 EventCognition으로 넘김 — 위치는 소음 발생 지점.
                    const FPerceptionData Perception(SourceName, ESenseType::Hearing, Stimulus.StimulusLocation,
                                                     OwnerNPC->GetActorLocation(), FinalDanger);
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
                            RequestJevDecision();
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
            StopSightTracking();
        }
    }
}

void ASmartNPCAIController::OnPerceptionTick()
{
    AActor* Target = CurrentSightTarget.Get();
    if (!Target)
    {
        StopSightTracking();
        return;
    }

    ASmartNPC* OwnerNPC = Cast<ASmartNPC>(GetPawn());
    if (!OwnerNPC) return;

    UNPCStateComponent* StateComp = OwnerNPC->StateComponent;
    if (!StateComp) return;

    const FString TargetID = ASmartNPC::PerceptionIdFor(Target);

    const FPerceptionData Perception(TargetID, ESenseType::Sight, Target->GetActorLocation(),
                                     OwnerNPC->GetActorLocation(), StateComp->ComputePerceptionDanger(SightBaseDanger, TargetID));

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

// ─────────────────────────────────────────────────────────────────────────────
// Jevlike 전술 편향기 — 요청/응답/워치독. 캐시·세대 카운터의 유일한 쓰기 지점.
// ─────────────────────────────────────────────────────────────────────────────

FJevDecision ASmartNPCAIController::GetFreshJevDecision() const
{
    const bool bFresh = JevDecision.ReceivedAt >= 0.0
        && (FPlatformTime::Seconds() - JevDecision.ReceivedAt) < static_cast<double>(JevDecisionTTL);
    return bFresh ? JevDecision : FJevDecision();
}

void ASmartNPCAIController::RequestJevDecision()
{
    UWorld* World = GetWorld();
    if (!World || bJevRequestPending) return; // 이미 예약됨 — 전송 시점에 최신 지표로 계산되므로 합쳐도 손실 없음

    // 적 둘이 거의 동시에 보이면 첫 감지 순간엔 한 명만 인지된 상태다. 버리지 않고 예약해 두면
    // 전송 시점엔 둘 다 반영된다. 같은 프레임 호출은 다음 틱 1건, 쿨다운 중이면 만료 시점 1건.
    bJevRequestPending = true;
    const double Wait = static_cast<double>(JevRequestCooldown) - (FPlatformTime::Seconds() - LastJevRequestTime);
    if (Wait > 0.0)
    {
        World->GetTimerManager().SetTimer(JevPendingTimer, this, &ASmartNPCAIController::SendCombatJevRequest, static_cast<float>(Wait), false);
    }
    else
    {
        JevPendingTimer = World->GetTimerManager().SetTimerForNextTick(this, &ASmartNPCAIController::SendCombatJevRequest);
    }
}

void ASmartNPCAIController::SendCombatJevRequest()
{
    bJevRequestPending = false;
    ASmartNPC* NPC = Cast<ASmartNPC>(GetPawn());
    UWorld* World = GetWorld();
    if (!NPC || !World || !NPC->StateComponent) return;

    // 진행 중 요청(전투·daily)이 있어도 막지 않는다 — SendJevQuery 의 세대 증가로 이전 응답은 stale 폐기되고
    // 워치독도 새로 걸린다. 최신 상황이 항상 이긴다.
    const double Now = FPlatformTime::Seconds();

    UNPCManager* Manager = UNPCManager::Get(this);
    if (!Manager || !Manager->IsServerConnected()) return; // 미연결 — 승수 1.0 중립 그대로

    UNPCStateComponent* StateComp = NPC->StateComponent;
    const FVector OwnerLoc = NPC->GetActorLocation();

    // ── 정규화 지표 사전 계산(좌표·절대 HP 금지 — 비율/미터/개수/불리언만) ──
    const float HpPct = FMath::Clamp(StateComp->GetAttributes().Resources.GetHealthPercent(), 0.f, 1.f);

    AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(Key_TargetActor)) : nullptr;
    const float DistanceM = Target ? FVector::Dist(OwnerLoc, Target->GetActorLocation()) / 100.f : 5.f;

    // 현재 시야 안 적대 대상 수 + 포위 여부(두 적의 방향이 90° 이상 벌어지면 포위).
    TArray<FVector> HostileDirs;
    if (PerceptionComp)
    {
        TArray<AActor*> Perceived;
        PerceptionComp->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);
        for (AActor* A : Perceived)
        {
            if (!A || A == NPC || IsTargetDead(A)) continue;
            if (A != Target && StateComp->GetRelation(ASmartNPC::PerceptionIdFor(A)) != ENPCRelation::Hostile) continue;
            HostileDirs.Add((A->GetActorLocation() - OwnerLoc).GetSafeNormal2D());
        }
    }
    if (Target && HostileDirs.Num() == 0)
    {
        HostileDirs.Add((Target->GetActorLocation() - OwnerLoc).GetSafeNormal2D());
    }
    bool bFlanked = false;
    for (int32 i = 0; i < HostileDirs.Num() && !bFlanked; ++i)
    {
        for (int32 j = i + 1; j < HostileDirs.Num(); ++j)
        {
            if (FVector::DotProduct(HostileDirs[i], HostileDirs[j]) < 0.f) { bFlanked = true; break; }
        }
    }

    LastJevRequestTime = Now;

    TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
    Metrics->SetNumberField(TEXT("hp_pct"),      HpPct);
    Metrics->SetNumberField(TEXT("distance_m"),  DistanceM);
    Metrics->SetNumberField(TEXT("enemy_count"), FMath::Max(1, HostileDirs.Num()));
    Metrics->SetBoolField  (TEXT("is_flanked"),  bFlanked);
    // 성격(0~1) — 포위·수적 열세에서 공격성·배짱이 높으면 Jev 가 돌파 공격 확률을 올린다(Python apply_personality).
    const FBehavioralTraits& Traits = StateComp->GetAttributes().Behavior;
    Metrics->SetNumberField(TEXT("aggression"),  FMath::Clamp(Traits.Aggression / 100.f, 0.f, 1.f));
    Metrics->SetNumberField(TEXT("bravery"),     FMath::Clamp(Traits.Bravery / 100.f, 0.f, 1.f));

    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("npc_id"),     NPC->AgentID);
    Payload->SetObjectField(TEXT("metrics"),    Metrics);

    SendJevQuery(Payload, false);
    UE_LOG(LogTemp, Verbose, TEXT("[Jev] %s gen=%u 요청 hp=%.2f dist=%.1f count=%d flanked=%d"),
        *NPC->AgentID, JevGeneration, HpPct, DistanceM, HostileDirs.Num(), bFlanked ? 1 : 0);
}

void ASmartNPCAIController::HandleJevDecisionResponse(const TSharedPtr<FJsonObject>& Payload)
{
    if (!Payload.IsValid()) return;

    // 세대 불일치(늦은 패킷) 또는 워치독이 이미 닫은 요청 → 폐기.
    int32 Generation = 0;
    Payload->TryGetNumberField(TEXT("generation"), Generation);
    if (!bJevRequestInFlight || static_cast<uint32>(Generation) != JevGeneration)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Jev] stale 응답 폐기 gen=%d (현재 %u, inflight=%d)"),
            Generation, JevGeneration, bJevRequestInFlight ? 1 : 0);
        return;
    }

    bJevRequestInFlight = false;
    if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(JevTimeoutTimer);

    // daily 응답 — 전투 승수 캐시와 무관. 받은 즉시 컴포넌트가 조립·주입한다.
    if (bJevInFlightDaily)
    {
        bJevInFlightDaily = false;
        if (ASmartNPC* NPC = Cast<ASmartNPC>(GetPawn()))
        {
            if (UNPCActionComponent* ActionComp = NPC->GetActionComponent()) ActionComp->ApplyJevDaily(*Payload);
        }
        return;
    }

    double Confidence = 0.0;
    Payload->TryGetNumberField(TEXT("confidence"), Confidence);
    if (Confidence < static_cast<double>(JevMinConfidence))
    {
        // 저신뢰도 — 편향 미적용(중립 1.0 유지). 이전 캐시도 지워 오래된 기조가 잔존하지 않게.
        JevDecision = FJevDecision();
        return;
    }

    FString StanceStr;
    Payload->TryGetStringField(TEXT("stance"), StanceStr);
    double Aggr = 1.0, Caution = 1.0, Noul = 0.0;
    Payload->TryGetNumberField(TEXT("score_aggression"), Aggr);
    Payload->TryGetNumberField(TEXT("score_caution"),    Caution);
    Payload->TryGetNumberField(TEXT("noul_harmful"),     Noul);

    JevDecision.Stance          = ParseJevStance(StanceStr);
    JevDecision.Confidence      = static_cast<float>(Confidence);
    JevDecision.ScoreAggression = static_cast<float>(Aggr);
    JevDecision.ScoreCaution    = static_cast<float>(Caution);
    JevDecision.NoulHarmful     = static_cast<float>(FMath::Clamp(Noul, 0.0, 1.0));
    JevDecision.ReceivedAt      = FPlatformTime::Seconds();

    UE_LOG(LogTemp, Log, TEXT("[Jev] %s gen=%u ← %s conf=%.2f aggr=%.2f caution=%.2f noul=%.2f"),
        *ASmartNPC::PerceptionIdFor(GetPawn()), JevGeneration, *StanceStr, Confidence, Aggr, Caution, Noul);
}

void ASmartNPCAIController::HandleJevTimeout()
{
    if (!bJevRequestInFlight) return;
    // 세대를 올려 이후 도착하는 같은 세대 패킷을 폐기. 캐시는 건드리지 않음 — TTL 이 자연 만료시킨다.
    ++JevGeneration;
    bJevRequestInFlight = false;
    // daily 타임아웃 = stay(현행 Idle). Idle 경과는 요청 때 이미 다시 재기 시작했다.
    bJevInFlightDaily = false;
    UE_LOG(LogTemp, Verbose, TEXT("[Jev] 워치독 타임아웃(%.2fs) — gen→%u, 중립 유지"), JevTimeoutSeconds, JevGeneration);
}

void ASmartNPCAIController::SendJevQuery(const TSharedRef<FJsonObject>& Payload, bool bDaily)
{
    UWorld* World = GetWorld();
    UNPCManager* Manager = UNPCManager::Get(this);
    if (!World || !Manager) return;

    ++JevGeneration;
    bJevRequestInFlight = true;
    bJevInFlightDaily = bDaily;
    World->GetTimerManager().SetTimer(JevTimeoutTimer, this, &ASmartNPCAIController::HandleJevTimeout, JevTimeoutSeconds, false);

    Payload->SetNumberField(TEXT("generation"), static_cast<double>(JevGeneration));
    Manager->SendEnvelopePromptToLLM(FEnvelopeBuilder::BuildJevQuery(Payload));
}

void ASmartNPCAIController::TickJevDaily()
{
    const double Now = FPlatformTime::Seconds();
    if (JevDailyIdleSince < 0.0)
    {
        JevDailyIdleSince = Now;
        return;
    }
    if (Now - JevDailyIdleSince < static_cast<double>(JevDailyIdleSeconds) || bJevRequestInFlight || bJevRequestPending) return;

    ASmartNPC* NPC = Cast<ASmartNPC>(GetPawn());
    UNPCActionComponent* ActionComp = NPC ? NPC->GetActionComponent() : nullptr;
    if (!ActionComp) return;
    if (Now - ActionComp->GetLastLLMBatchTime() < static_cast<double>(JevDailyAfterLLMSeconds)) return;

    // 미연결이면 조용히 Idle 유지(경고 스팸 금지) — 재연결되면 다음 틱에 바로 요청한다.
    UNPCManager* Manager = UNPCManager::Get(this);
    if (!Manager || !Manager->IsServerConnected()) return;

    const float IdleS = static_cast<float>(Now - JevDailyIdleSince);
    // 요청 후 다시 잰다 — stay 가 오면 N초 뒤 재요청, 활동이 오면 그 활동이 끝난 뒤부터 잰다.
    JevDailyIdleSince = Now;

    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("npc_id"), NPC->AgentID);
    Payload->SetStringField(TEXT("domain"), TEXT("daily"));
    ActionComp->BuildJevDailyQuery(Manager->CollectNearbyContext(NPC), IdleS, *Payload);

    SendJevQuery(Payload, true);
    UE_LOG(LogTemp, Verbose, TEXT("[Jev] %s gen=%u daily 요청 idle=%.1fs"), *NPC->AgentID, JevGeneration, IdleS);
}
