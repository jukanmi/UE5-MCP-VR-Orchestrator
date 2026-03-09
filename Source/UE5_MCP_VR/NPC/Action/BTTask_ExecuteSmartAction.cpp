#include "BTTask_ExecuteSmartAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"
#include "../Struct/NPCActionKeys.h"

namespace
{
    // [의도(Why)] 블랙보드에 직렬화되어 있는 JSON 문자열 파라미터를 파싱해 로직에서 사용하기 쉬운 Map 형태로 변환합니다. JSON 변환 실패 시 빈 Map 반환을 보장하여 전체 로직의 런타임 예외를 방지합니다.
    TMap<FString, FString> ParseParametersFromJson(const FString& JsonString)
    {
        TMap<FString, FString> ResultMap;
        if (JsonString.IsEmpty()) return ResultMap;

        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        TSharedPtr<FJsonObject> JsonObj;
        
        if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
        {
            for (const auto& Pair : JsonObj->Values)
            {
                if (Pair.Value->Type != EJson::String) continue;
                ResultMap.Add(Pair.Key, Pair.Value->AsString());
            }
        }
        return ResultMap;
    }
}

UBTTask_ExecuteSmartAction::UBTTask_ExecuteSmartAction()
{
	NodeName = TEXT("Execute Smart Action");
}

EBTNodeResult::Type UBTTask_ExecuteSmartAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ASmartNPCAIController* AICon = Cast<ASmartNPCAIController>(OwnerComp.GetAIOwner());
	if (!AICon) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AICon->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
	
	// [의도(Why)] 파라미터(Type, Target, Params Map)의 라우팅 책임을 온전히 NPCActionComponent에 단일 위임(Delegate)
	// Blackboard를 경유한 JSON 직렬화/역직렬화 오버헤드를 방지하고 컴포넌트의 CurrentAction을 직접 참조합니다.
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        const FGameAction& Action = ActionComp->GetCurrentAction();
        ActionComp->ExecuteInteraction(Action.ActionType, TargetActor, Action.Parameters);
    }

	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString UBTTask_ExecuteSmartAction::GetStaticDescription() const
{
	return TEXT("단일화된 EAction 기반 NPC 명령을 싱글톤 라우팅합니다.");
}
