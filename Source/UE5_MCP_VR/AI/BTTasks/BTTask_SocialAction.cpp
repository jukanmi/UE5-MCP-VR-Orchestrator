#include "BTTask_SocialAction.h"
#include "BTTask_CommonAction.h"  // CommonFallback 접근용
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "../SmartNPC.h"
#include "../SmartNPCAIController.h"
#include "../NPCActionKeys.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UBTTask_SocialAction::UBTTask_SocialAction()
{
	NodeName = "Social Action";
	SubAction = ESocialAction::Emote;
}

EBTNodeResult::Type UBTTask_SocialAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
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

	// Social Enum에서 찾기
	const UEnum* EnumPtr = StaticEnum<ESocialAction>();
	int64 EnumValue = EnumPtr->GetValueByName(FName(*SubActionStr));
	if (EnumValue == INDEX_NONE)
	{
		EnumValue = EnumPtr->GetValueByName(FName(*FString::Printf(TEXT("ESocialAction::%s"), *SubActionStr)));
	}

	// ─────────────────────────────────────────────────────────────────
	// [Fallback] Social Enum에 없는 액션 → CommonAction으로 위임
	// 왜: 사교 중에도 "Move", "Wait" 같은 기본 행동이 빈번함.
	// 예시: 대화 후 "걸어가면서 손 흔들기" → Move(Common) + Emote(Social)
	// ─────────────────────────────────────────────────────────────────
	if (EnumValue == INDEX_NONE)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[SocialAction] '%s' not in ESocialAction, delegating to CommonFallback"),
			*SubActionStr);
		return UBTTask_CommonAction::ExecuteCommonFallback(OwnerComp, TEXT("SocialAction"));
	}

	ESocialAction Action = static_cast<ESocialAction>(EnumValue);
	SubAction = Action;

	// Social 전용 로직
	switch (Action)
	{
	case ESocialAction::Emote:
		{
			FString GestureType = Params.FindRef(TEXT("GestureType"));
			if (GestureType.IsEmpty()) GestureType = Params.FindRef(TEXT("gesture"));
			NPC->ExecuteEmote(GestureType);
		}
		break;
	
	case ESocialAction::Follow:
		{
			AActor* TargetActor = Cast<AActor>(BB->GetValueAsObject(ASmartNPCAIController::Key_TargetActor));
			float Distance = 200.0f;
			if (Params.Contains(TEXT("distance"))) Distance = FCString::Atof(*Params[TEXT("distance")]);
			
			if (TargetActor)
			{
				NPC->ExecuteKeepDistance(TargetActor, Distance);
			}
			else
			{
				// Actor가 없으면 좌표로 이동 (폴백)
				FVector TargetLoc = BB->GetValueAsVector(ASmartNPCAIController::Key_TargetLocation);
				NPC->ExecuteMoveToLocation(TargetLoc, EMoveType::Run);
			}
		}
		break;
	
	case ESocialAction::Trade:
	case ESocialAction::GiveItem:
	case ESocialAction::Comfort:
	case ESocialAction::HandObject:
	default:
		{
			// 아직 미구현 Social 액션들은 GenericAction + 로그로 처리
			FString TargetID = Params.FindRef(NPCActionKeys::Key_TargetID);
			UE_LOG(LogTemp, Warning,
				TEXT("[SocialAction] Unimplemented: %s (Target: %s) -> Executing generic Emote 'Talk'"),
				*SubActionStr, *TargetID);
			NPC->ExecuteEmote(TEXT("Talk"));
		}
		break;
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_SocialAction::GetStaticDescription() const
{
	if (UEnum* EnumPtr = StaticEnum<ESocialAction>())
	{
		return FString::Printf(TEXT("Execute Social Action: %s"), *EnumPtr->GetValueAsString(SubAction));
	}
	return TEXT("Execute Social Action");
}
