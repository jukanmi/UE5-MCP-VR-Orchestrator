#include "BTTask_ExecuteSmartAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "../SmartNPC.h"
#include "NPCActionComponent.h"
#include "../Struct/NPCActionKeys.h"

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

	// 블랙보드에서 JSON 파라미터 파싱
	FString ParamsJsonStr = BB->GetValueAsString(ASmartNPCAIController::Key_Parameters);
	TMap<FString, FString> Params;

	if (!ParamsJsonStr.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJsonStr);
		TSharedPtr<FJsonObject> JsonObj;
		if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
		{
			for (auto& Pair : JsonObj->Values)
			{
				if (Pair.Value->Type == EJson::String)
				{
					Params.Add(Pair.Key, Pair.Value->AsString());
				}
			}
		}
	}

    // 블랙보드에서 EAction 값을 직접 추출합니다.
	EAction ActionType = (EAction)BB->GetValueAsEnum(ASmartNPCAIController::Key_SubAction);

	// [Optional] FacialState 변환 처리
    // FModeActionRequest 명세 상 최상위에 FacialState가 오지만, 현재 C++에서는 Parameters Map에 담기어 오는 상황을 가정하거나
    // 향후 확장을 위해 NPCActionComponent에서 Facial State Setter를 호출할 수 있습니다.
	
	AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
	
	// 전체 파라미터 맵(Params)을 통째로 넘겨 하위 컴포넌트가 알아서 파싱하도록 책임을 위임합니다.
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        ActionComp->ExecuteInteraction(ActionType, TargetActor, Params);
    }

	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString UBTTask_ExecuteSmartAction::GetStaticDescription() const
{
	return TEXT("단일화된 EAction 기반 NPC 명령을 싱글톤 라우팅합니다.");
}
