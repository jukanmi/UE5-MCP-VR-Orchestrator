#include "BTTask_LifestyleAction.h"
#include "BTTask_CommonAction.h"  // CommonFallback 접근용
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "../NPCActionKeys.h"

UBTTask_LifestyleAction::UBTTask_LifestyleAction()
{
	NodeName = "Lifestyle Action";
	SubAction = ELifestyleAction::Sit;
}

EBTNodeResult::Type UBTTask_LifestyleAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController) return EBTNodeResult::Failed;

	ASmartNPC* NPC = Cast<ASmartNPC>(AIController->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	// 공용 헬퍼로 파라미터 파싱 (중복 코드 제거)
	FString SubActionStr;
	TMap<FString, FString> Params;
	UBTTask_CommonAction::ParseBlackboardParams(BB, Params, SubActionStr);

	// Lifestyle Enum에서 찾기
	const UEnum* EnumPtr = StaticEnum<ELifestyleAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ELifestyleAction::%s"), *SubActionStr)));

	// ─────────────────────────────────────────────────────────────────
	// [Fallback] Lifestyle Enum에 없는 액션 → CommonAction으로 위임
	// 왜: 독서 중에도 "Dialogue"(혼잣말), "TurnTo"(소리 방향) 같은 Common 필요
	// ─────────────────────────────────────────────────────────────────
	if (EnumValue == INDEX_NONE)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[LifestyleAction] '%s' not in ELifestyleAction, delegating to CommonFallback"),
			*SubActionStr);
		return UBTTask_CommonAction::ExecuteCommonFallback(OwnerComp, TEXT("LifestyleAction"));
	}

	ELifestyleAction Action = static_cast<ELifestyleAction>(EnumValue);
	SubAction = Action;

	// Lifestyle 전용 로직
	switch (Action)
	{
	case ELifestyleAction::Dance:
	case ELifestyleAction::Sing:
		// 감정 표현이 필요한 행동 → Emote 시스템 사용
		NPC->ExecuteEmote(SubActionStr);
		break;
	
	case ELifestyleAction::Sit:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			FString TargetID = Params.FindRef(TEXT("ChairID"));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_SitDown, TargetActor, TargetID, TEXT(""));
		}
		break;

	case ELifestyleAction::Sleep:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			FString TargetID = Params.FindRef(TEXT("BedID"));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_LieDown, TargetActor, TargetID, TEXT(""));
			//ToDo:: 눈 감기
		}
		break;

	case ELifestyleAction::Clean:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			FString TargetID = Params.FindRef(TEXT("Area"));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_Clean, TargetActor, TargetID, TEXT(""));
		}
		break;

	case ELifestyleAction::Read:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			FString TargetID = Params.FindRef(TEXT("BookID"));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_Read, TargetActor, TargetID, TEXT(""));
		}
		break;

	case ELifestyleAction::Pray:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			FString TargetID = Params.FindRef(TEXT("Area"));
			NPC->ExecuteInteraction(NPCActionKeys::Interact_Pray, TargetActor, TargetID, TEXT(""));
		}
		break;

	default:
		{
			// Unknown Lifestyle Action
		}
		break;
	}

	// 액션 완료 → 큐 진행
	BB->ClearValue(ASmartNPCAIController::Key_SubAction);
	NPC->OnActionCompleted();

	return EBTNodeResult::Succeeded;
}

FString UBTTask_LifestyleAction::GetStaticDescription() const
{
	return FString::Printf(TEXT("Execute Lifestyle Action: %s"), *UEnum::GetValueAsString(SubAction));
}
