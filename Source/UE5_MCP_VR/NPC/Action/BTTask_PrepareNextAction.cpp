#include "BTTask_PrepareNextAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"
#include "../Struct/NPCActionKeys.h"
#include "Perception/AIPerceptionComponent.h"
#include "AIController.h"

namespace
{
    /** AIController의 퍼셉션 컴포넌트에서 현재 인지 중인 적 위치를 수집 */
    TArray<FVector> GetPerceivedEnemyLocations(ASmartNPCAIController* AICon)
    {
        TArray<FVector> Locations;
        if (!AICon) return Locations;

        UAIPerceptionComponent* PercComp = AICon->FindComponentByClass<UAIPerceptionComponent>();
        if (!PercComp) return Locations;

        TArray<AActor*> PerceivedActors;
        PercComp->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);

        for (AActor* Actor : PerceivedActors)
        {
            if (!Actor || Actor == AICon->GetPawn()) continue;
            Locations.Add(Actor->GetActorLocation());
        }
        return Locations;
    }
}

UBTTask_PrepareNextAction::UBTTask_PrepareNextAction()
{
	NodeName = TEXT("Prepare Next Action");
    // TickTask 활성화: 전술 쿼리 완료를 폴링하기 위해 필요
    bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_PrepareNextAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ASmartNPCAIController* AICon = Cast<ASmartNPCAIController>(OwnerComp.GetAIOwner());
	if (!AICon) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AICon->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	UNPCActionComponent* ActionComp = NPC->GetActionComponent();
	if (!ActionComp) return EBTNodeResult::Failed;

    FBTPrepareNodeMemory* Memory = reinterpret_cast<FBTPrepareNodeMemory*>(NodeMemory);
    Memory->bRunningTacticalQuery = false;

	// 이미 진행 중인 액션이 있으면 방어 처리
	if (ActionComp->bIsBusy) return EBTNodeResult::Failed;

	// ── 경로 1: 큐에 액션이 있으면 기존 로직 ──────────────────────────────
	if (ActionComp->ProcessNextAction()) return EBTNodeResult::Succeeded;

    // 큐 비어 있음 → Blackboard HasAction 내림
    if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
    {
        BB->SetValueAsBool(ASmartNPCAIController::Key_HasAction, false);
        BB->SetValueAsEnum(ASmartNPCAIController::Key_SubAction, (uint8)EAction::Idle);
    }

	// ── 경로 2: 적이 인지되어 있으면 전술 쿼리 시작 ────────────────────────
    const TArray<FVector> EnemyLocs = GetPerceivedEnemyLocations(AICon);
    if (EnemyLocs.IsEmpty()) return EBTNodeResult::Failed;

    // 이전 쿼리 상태 초기화 후 시작
    ActionComp->TacticalQueryState = ETacticalQueryState::Idle;
    ActionComp->StartTacticalQuery(EnemyLocs);

    // EQS 에셋 없음 등으로 즉시 실패한 경우
    if (ActionComp->TacticalQueryState == ETacticalQueryState::Failed)
        return EBTNodeResult::Failed;

    Memory->bRunningTacticalQuery = true;
    return EBTNodeResult::InProgress;
}

void UBTTask_PrepareNextAction::TickTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    FBTPrepareNodeMemory* Memory = reinterpret_cast<FBTPrepareNodeMemory*>(NodeMemory);
    if (!Memory->bRunningTacticalQuery)
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    ASmartNPCAIController* AICon = Cast<ASmartNPCAIController>(OwnerComp.GetAIOwner());
    ASmartNPC* NPC = AICon ? Cast<ASmartNPC>(AICon->GetPawn()) : nullptr;
    UNPCActionComponent* ActionComp = NPC ? NPC->GetActionComponent() : nullptr;
    if (!ActionComp) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

    switch (ActionComp->TacticalQueryState)
    {
    case ETacticalQueryState::ResultReady:
    {
        // LLM이 선택한 위치가 큐에 이미 주입됨 → ProcessNextAction 으로 BB 세팅
        if (ActionComp->ProcessNextAction())
        {
            ActionComp->TacticalQueryState = ETacticalQueryState::Idle;
            Memory->bRunningTacticalQuery = false;
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        }
        else
        {
            ActionComp->TacticalQueryState = ETacticalQueryState::Idle;
            FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        }
        break;
    }
    case ETacticalQueryState::Failed:
        Memory->bRunningTacticalQuery = false;
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        break;

    default:
        // WaitingEQS / WaitingLLM → 계속 대기
        break;
    }
}

EBTNodeResult::Type UBTTask_PrepareNextAction::AbortTask(
    UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    FBTPrepareNodeMemory* Memory = reinterpret_cast<FBTPrepareNodeMemory*>(NodeMemory);
    if (Memory->bRunningTacticalQuery)
    {
        ASmartNPCAIController* AICon = Cast<ASmartNPCAIController>(OwnerComp.GetAIOwner());
        ASmartNPC* NPC = AICon ? Cast<ASmartNPC>(AICon->GetPawn()) : nullptr;
        if (UNPCActionComponent* ActionComp = NPC ? NPC->GetActionComponent() : nullptr)
        {
            ActionComp->TacticalQueryState = ETacticalQueryState::Idle;
            ActionComp->TacticalCandidateMap.Empty();
        }
        Memory->bRunningTacticalQuery = false;
    }
    return EBTNodeResult::Aborted;
}

FString UBTTask_PrepareNextAction::GetStaticDescription() const
{
	return TEXT("큐 액션 Dequeue 또는 EQS+LLM 전술 위치 결정 후 BT 분기 준비");
}
