#include "BTTask_ExecuteSmartAction.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmartNPCAIController.h"
#include "SmartNPC.h"
#include "../Component/NPCActionComponent.h"
#include "NPCActionKeys.h"

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
	FString ParamsJsonStr = BB->GetValueAsString(ASmartNPCAIController::Key_ActionParameters);
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

    // 블랙보드에서 EAction 문자열 추출
	FString ActionStr = BB->GetValueAsString(ASmartNPCAIController::Key_SubAction);
	
    // EAction으로 변환
    EAction ActionType = EAction::Idle;
    if (UEnum* ActionEnum = StaticEnum<EAction>())
    {
        if (!ActionStr.IsEmpty())
        {
            int64 EnumVal = ActionEnum->GetValueByNameString(ActionStr);
            if (EnumVal != INDEX_NONE) ActionType = static_cast<EAction>(EnumVal);
        }
    }

	// [Optional] FacialState 변환 처리
    // FModeActionRequest 명세 상 최상위에 FacialState가 오지만, 현재 C++에서는 Parameters Map에 담기어 오는 상황을 가정하거나
    // 향후 확장을 위해 NPCActionComponent에서 Facial State Setter를 호출할 수 있습니다.
	
	AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
	
    // TODO: Parameter 내부의 세부 항목(TargetID, Speed 등)들은 NPCActionComponent 쪽에서 꺼내 쓰거나 
    // ExecuteInteraction 내부에서 직접 접근하도록 파서를 위임할 필요가 있음.
	// 우선 현재 구조에서는 TargetID를 보냅니다.
	FString TargetID = Params.FindRef(TEXT("TargetID"));
    if (TargetID.IsEmpty())
    {
        TargetID = Params.FindRef(TEXT("ItemID")); // 아이템 ID일 경우 Fallback
    }

	// 핵심 래퍼 호출
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        ActionComp->ExecuteInteraction(ActionType, TargetActor, TargetID);
    }

	NPC->OnActionCompleted();
	return EBTNodeResult::Succeeded;
}

FString UBTTask_ExecuteSmartAction::GetStaticDescription() const
{
	return TEXT("단일화된 EAction 기반 NPC 명령을 싱글톤 라우팅합니다.");
}
