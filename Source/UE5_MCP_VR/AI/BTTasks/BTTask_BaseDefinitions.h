#pragma once

#include "CoreMinimal.h"
#include "BTTask_BaseDefinitions.generated.h"

/**
 * ============================================================================
 * EFacialState - NPC 표정 상태 (9가지)
 * ============================================================================
 * 
 * Dialogue Agent가 결정하는 NPC의 표정 상태입니다.
 * AnimBlueprint에서 이 값을 읽어 Facial Blend Shape를 제어합니다.
 * 
 * 사용 예시:
 * - Dialogue Agent: "Player가 도움을 요청함" → FacialState = Happy
 * - AnimBlueprint: EFacialState::Happy → 입꼬리 올림, 눈 웃음
 */
UENUM(BlueprintType)
enum class EFacialState : uint8
{
	Neutral         UMETA(DisplayName = "Neutral"),         // 평온 (기본)
	Happy           UMETA(DisplayName = "Happy"),           // 기쁨 (입꼬리 올라감, 눈 웃음)
	Sad             UMETA(DisplayName = "Sad"),             // 슬픔 (눈썹 처짐, 입꼬리 내려감)
	Angry           UMETA(DisplayName = "Angry"),           // 분노 (미간 찌푸림)
	Fear            UMETA(DisplayName = "Fear"),            // 공포 (눈 커짐, 입 벌림)
	Surprised       UMETA(DisplayName = "Surprised"),       // 놀람
	Disgusted       UMETA(DisplayName = "Disgusted"),       // 혐오
	Tired           UMETA(DisplayName = "Tired"),           // 피곤 (눈 반쯤 감기)
	Pain            UMETA(DisplayName = "Pain")             // 고통
};

/**
 * ============================================================================
 * ENPCBehaviorMode - NPC 행동 모드 (5가지 + None)
 * ============================================================================
 * 
 * Dialogue Agent가 결정하는 NPC의 최상위 행동 카테고리입니다.
 * Behavior Tree에서 이 값에 따라 서브트리를 분기합니다.
 * 
 * 파이프라인:
 * 1. Dialogue Agent: 상황 분석 → Mode 결정 (예: Combat)
 * 2. Interface Output Agent: Mode 내 세부 액션 결정 (예: Attack_Melee)
 * 3. UE5 Behavior Tree: Mode로 서브트리 선택 → 세부 액션 실행
 * 
 * 각 Mode별 세부 액션은 별도 파일에 정의:
 * - Combat: BTTask_CombatActions.h
 * - Social: BTTask_SocialActions.h
 * - Task: BTTask_TaskActions.h
 * - Investigation: BTTask_InvestigationActions.h
 * - Lifestyle: BTTask_LifestyleActions.h
 */
UENUM(BlueprintType)
enum class ENPCBehaviorMode : uint8
{
	None           UMETA(DisplayName = "None"),            // 초기 상태 / 오류
	Combat         UMETA(DisplayName = "Combat"),          // 전투 모드
	Social         UMETA(DisplayName = "Social"),          // 사교 모드
	Task           UMETA(DisplayName = "Task"),            // 상호작용/작업 모드
	Investigation  UMETA(DisplayName = "Investigation"),   // 탐색/조사 모드
	Lifestyle      UMETA(DisplayName = "Lifestyle"),       // 생활/대기 모드
	Common         UMETA(DisplayName = "Common")           // 공용/기본 액션 모드
};

/**
 * ============================================================================
 * FModeActionRequest - Cognitive Engine → UE5 전달 구조체
 * ============================================================================
 * 
 * Dialogue Agent와 Interface Output Agent가 결정한 정보를 UE5로 전달합니다.
 * 
 * 데이터 흐름:
 * 1. Dialogue Agent:
 *    - Mode: Social
 *    - FacialState: Happy
 *    - raw_response: "알겠어요! (기쁘게) *달려가며*"
 * 
 * 2. Interface Output Agent:
 *    - ActionName: "Dialogue" (Social 모드 내 세부 액션)
 *    - TargetID: "Player"
 *    - Parameters: {Tone: "Friendly"}
 * 
 * 3. UE5 SmartNPC::ProcessAction():
 *    - Blackboard->SetValueAsEnum(Key_BehaviorMode, Mode)
 *    - Blackboard->SetValueAsEnum(Key_FacialState, FacialState)
 *    - Blackboard->SetValueAsString(Key_SubAction, ActionName)
 *    - Blackboard->SetValueAsString(Key_ActionParameters, Parameters JSON)
 * 
 * 4. Behavior Tree:
 *    - Mode == Social → Social 서브트리 실행
 *    - SubAction == "Dialogue" → BTTask_Dialogue 노드 실행
 *    - AnimBlueprint: FacialState == Happy → 표정 변경
 */
USTRUCT(BlueprintType)
struct FModeActionRequest
{
	GENERATED_BODY()

	/** 행동 모드 큰 틀 (Dialogue Agent가 결정) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	ENPCBehaviorMode Mode = ENPCBehaviorMode::None;

	/** 표정 상태 (Dialogue Agent가 결정, 필수) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	EFacialState FacialState = EFacialState::Neutral;

	/** 세부 액션 이름 (Interface Output Agent가 결정) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	FString ActionName;

	/** 대상 Actor ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	FString TargetID;

	/** 유연한 파라미터 맵 (액션별로 다른 키 사용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Mode")
	TMap<FString, FString> Parameters;
};
