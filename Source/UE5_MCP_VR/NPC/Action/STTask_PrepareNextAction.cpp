#include "STTask_PrepareNextAction.h"
#include "StateTreeLinker.h"
#include "StateTreeExecutionContext.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"
#include "../Struct/NPCActionKeys.h"

bool FSTTask_PrepareNextAction::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

EStateTreeRunStatus FSTTask_PrepareNextAction::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);

    // EnterState 시 한 번만 Cast → InstanceData에 캐싱. 이후 Tick은 캐시된 포인터 사용.
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    if (ASmartNPC* NPC = Cast<ASmartNPC>(AICon.GetPawn()))
    {
        Data.CachedActionComp = NPC->GetActionComponent();
    }

    return Tick(Context, 0.f);
}

EStateTreeRunStatus FSTTask_PrepareNextAction::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    Data.bHasAction = false;

    UNPCActionComponent* ActionComp = Data.CachedActionComp.Get();
    if (!ActionComp) return EStateTreeRunStatus::Running; // NPC pending destroy 등

    // --- 전투 종료 게이트 (2026-07-11) ---
    // Combat 중 타겟 사망 시 즉시 전투 해제. bIsBusy 체크보다 앞서 실행해
    // 진행 중인 스윙·이동도 그 자리에서 끊는다(시체·리스폰 플레이어 무한공격 제거).
    // BB 클리어·모드 복귀는 컨트롤러 핸들러 경유(BB 쓰기는 컨트롤러 소유 — STTask 직접 쓰기 금지).
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    AActor* CombatTarget = nullptr;
    if (ActionComp->GetBehaviorMode() == ENPCBehaviorMode::Combat)
    {
        if (UBlackboardComponent* BB = AICon.GetBlackboardComponent())
        {
            CombatTarget = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
        }
        if (CombatTarget && ASmartNPCAIController::IsTargetDead(CombatTarget))
        {
            AICon.HandleCombatTargetDead(CombatTarget);
            return EStateTreeRunStatus::Running;
        }
    }

    // 이전 액션 진행 중이면 대기 — OnActionCompleted가 bIsBusy=false로 풀어줄 때까지.
    if (ActionComp->bIsBusy) return EStateTreeRunStatus::Running;

    // 큐에 액션 있으면 Dequeue → 다음 State로
    if (ActionComp->ProcessNextAction())
    {
        const FGameAction& Action = ActionComp->GetCurrentAction();
        Data.bHasAction = true;
        Data.SubAction = Action.ActionType;

        if (const FString* LocStr = Action.Parameters.Find(NPCActionKeys::Key_TargetLoc))
        {
            FVector Loc;
            if (!LocStr->IsEmpty() && Loc.InitFromString(*LocStr))
            {
                Data.TargetLocation = Loc;
            }
        }
        return EStateTreeRunStatus::Succeeded;
    }

    // 큐 비어있음 — 자율 행동 주입 정책:
    //   비전투(2026-05-16 결정 유지): 자동 Track 없음. LLM 명시 액션 없으면 Idle 유지.
    //   Combat(2026-07-11 변경): Attack 고정 자동주입 → C++ 척수 셀렉터. 가중치·주사위로
    //   Attack/Dodge/Block/거리조절/Flee/SignalAllies 를 주입하고 페이싱 간격도 셀렉터가 관리.
    if (CombatTarget)
    {
        ActionComp->SelectCombatAction(CombatTarget);
    }

    return EStateTreeRunStatus::Running;
}

// ============================================================================
// FSTEvaluator_NPCState
// ============================================================================

bool FSTEvaluator_NPCState::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

void FSTEvaluator_NPCState::TreeStart(FStateTreeExecutionContext& Context) const
{
    Tick(Context, 0.0f);
}

void FSTEvaluator_NPCState::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FSTEvaluator_NPCState_InstanceData& InstanceData = Context.GetInstanceData(*this);
    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);

    if (ASmartNPC* NPC = Cast<ASmartNPC>(AICon.GetPawn()))
    {
        if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
        {
            InstanceData.bHasAction = ActionComp->HasPendingActions() || ActionComp->bIsBusy;
            InstanceData.BehaviorMode = ActionComp->GetBehaviorMode();
        }
    }
}
