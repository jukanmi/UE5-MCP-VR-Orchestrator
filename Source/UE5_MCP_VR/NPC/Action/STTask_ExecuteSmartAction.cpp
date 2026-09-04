#include "STTask_ExecuteSmartAction.h"
#include "StateTreeLinker.h"
#include "StateTreeExecutionContext.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../BP/SmartNPC.h"
#include "NPCActionComponent.h"
#include "../NPCManager.h"
#include "../Struct/NPCActionKeys.h"
#include "../../Furniture/FurnitureManager.h"
#include "../../Furniture/BP/FurnitureActor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"

namespace
{
    // LLM target_id 의미키워드 → 실제 AActor*.
    //   Player → 플레이어 폰 / Self → 자신 / Enemy·빈값 → BB perception 타겟
    //   / 그 외 → NPCMap AgentID 조회 → FurnitureManager 가구 ID 조회 순.
    // 해석 실패 시 BB 타겟으로 폴백(기존 동작 보존).
    AActor* ResolveActionTarget(ASmartNPC* Self, const FString& Keyword, AActor* BBTarget)
    {
        if (Keyword.IsEmpty()) return BBTarget;
        if (Keyword.Equals(TEXT("Self"), ESearchCase::IgnoreCase)) return Self;
        if (Keyword.Equals(TEXT("Enemy"), ESearchCase::IgnoreCase)) return BBTarget;
        if (Keyword.Equals(TEXT("Player"), ESearchCase::IgnoreCase))
        {
            // GetPlayerPawn 은 유효 WorldContext 필요 — Self null 시 크래시 방지.
            if (Self)
            {
                if (AActor* P = UGameplayStatics::GetPlayerPawn(Self, 0)) return P;
            }
            return BBTarget;
        }
        // <NpcName> — NPCMap 조회
        if (UNPCManager* Mgr = UNPCManager::Get(Self))
        {
            if (AActor* Found = Cast<AActor>(Mgr->GetNPCById(Keyword)))
            {
                return Found;
            }
        }
        // <FurnitureID> — 가구 등록소 조회 (NPC 이름과 충돌 시 NPC 우선 — 위 분기가 먼저).
        if (UFurnitureManager* FurnMgr = UFurnitureManager::Get(Self))
        {
            if (AFurnitureActor* Found = FurnMgr->GetFurnitureByID(Keyword))
            {
                return Found;
            }
        }
        return BBTarget; // 미해석 폴백
    }
}

bool FSTTask_ExecuteSmartAction::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

EStateTreeRunStatus FSTTask_ExecuteSmartAction::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);

    ASmartNPCAIController& AICon = Context.GetExternalData(AIControllerHandle);
    ASmartNPC* NPC = Cast<ASmartNPC>(AICon.GetPawn());
    if (!NPC) return EStateTreeRunStatus::Failed;

    UNPCActionComponent* ActionComp = NPC->GetActionComponent();
    if (!ActionComp) return EStateTreeRunStatus::Failed;

    if (!ActionComp->bIsBusy)
    {
        UE_LOG(LogTemp, Warning, TEXT("[STTask_Execute] %s: no prepared action — skipping"), *NPC->GetName());
        return EStateTreeRunStatus::Failed;
    }

    Data.CachedActionComp = ActionComp;

    AActor* BBTarget = nullptr;
    if (UBlackboardComponent* BB = AICon.GetBlackboardComponent())
    {
        BBTarget = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
    }

    const FGameAction& Action = ActionComp->GetCurrentAction();
    // LLM target_id 키워드(Player/Self/Enemy/<NpcName>)를 실제 actor 로 해석. 없으면 BB 타겟.
    const FString TargetKeyword = Action.Parameters.FindRef(NPCActionKeys::Key_TargetID);
    AActor* TargetActor = ResolveActionTarget(NPC, TargetKeyword, BBTarget);
    // 실행만 발행. 즉시형 액션은 ExecuteInteraction 내부에서 OnActionCompleted(bIsBusy=false)까지 끝나고,
    // 비동기 액션(이동/몽타주)은 콜백이 완료시킬 때까지 bIsBusy=true가 유지된다.
    ActionComp->ExecuteInteraction(Action.ActionType, TargetActor, Action.Parameters);

    // 같은 프레임에 즉시형이 완료됐을 수 있으니 Tick으로 판정.
    return Tick(Context, 0.f);
}

EStateTreeRunStatus FSTTask_ExecuteSmartAction::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);

    UNPCActionComponent* ActionComp = Data.CachedActionComp.Get();
    if (!ActionComp) return EStateTreeRunStatus::Failed; // NPC pending destroy 등

    // 비동기 완료 대기 — 이동 도착/몽타주 종료/즉시형 완료/워치독 만료 시 bIsBusy=false.
    return ActionComp->bIsBusy ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}
