#include "NPC/Action/STEvaluator_JevTactics.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

bool FSTEvaluator_JevTactics::Link(FStateTreeLinker& Linker)
{
    Linker.LinkExternalData(AIControllerHandle);
    return true;
}

void FSTEvaluator_JevTactics::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Out = Context.GetInstanceData(*this);
    const ASmartNPCAIController& AIC = Context.GetExternalData(AIControllerHandle);

    // 0ms 값 복사만 — TTL 만료·미수신은 컨트롤러가 중립 기본값으로 돌려준다(오래된 도주 기조 오염 방지).
    const FJevDecision Decision = AIC.GetFreshJevDecision();
    Out.TacticalStance     = Decision.Stance;
    Out.Confidence         = Decision.Confidence;
    Out.HarmfulProbability = Decision.NoulHarmful;
}
