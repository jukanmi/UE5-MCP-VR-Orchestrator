#pragma once

#include "CoreMinimal.h"

/**
 * NPCActionKeys
 * 
 * [의도] NPC 행동(Action) 처리 시 사용되는 문자열 상수들을 중앙 집중화합니다.
 * 하드코딩된 문자열로 인한 오타 방지 및 유지보수성 향상을 목적으로 합니다.
 * 현재는 JSON 파싱, 디버그 데이터 생성, 핵심 상호작용(Interaction) 몽타주 매핑에 제한적으로 사용됩니다.
 */
namespace NPCActionKeys
{
    // --- JSON Keys (JSON 파싱용 키) ---
    inline const FString Key_ActionType = TEXT("action_type");
    inline const FString Key_TargetID   = TEXT("target_id");   
    inline const FString Key_Text       = TEXT("text");
    inline const FString Key_Emotion    = TEXT("emotion");
    inline const FString Key_Params     = TEXT("parameters");

    // --- Location Keys (위치 정보 키) ---
    // 좌표는 "target_loc": {"x":0, "y":0, "z":0} 형태로 한 세트로 전달됨
    inline const FString Key_TargetLoc  = TEXT("target_loc");
    inline const FString Loc_X          = TEXT("x");
    inline const FString Loc_Y          = TEXT("y");
    inline const FString Loc_Z          = TEXT("z");

    // [의도] 디버그 환경 등에서 더미 액션을 생성할 때 활용되는 기본 행동 유형 문자열입니다.
    // (참고: 실제 실행부(CPP)에서는 EAction 열거형(Enum)으로 치환되어 엄격하게 관리됩니다)
    inline const FString Action_Dialogue    = TEXT("Dialogue");
    inline const FString Action_Move        = TEXT("Move");
    inline const FString Action_Wait        = TEXT("Wait");
    
    // [의도] 행동 트리의 주요 카테고리를 식별하여 디버깅 및 특정 조건 처리에 사용됩니다.
    inline const FString Mode_Social        = TEXT("Social");
    inline const FString Mode_Combat        = TEXT("Combat");
    
    // [의도] NPCActionComponent에서 애니메이션 몽타주를 재생하기 위해 데이터 에셋과 매핑되는 고유 키워드들입니다.
    // 기존 String 기반 통신에서 발생하던 결합도를 낮추고 오직 몽타주 매핑 용도로 역할을 제한하였습니다.
    inline const FString Interact_SitDown   = TEXT("SitDown");
    inline const FString Interact_SitUp     = TEXT("SitUp");
    inline const FString Interact_LieDown   = TEXT("LieDown");
    inline const FString Interact_LieUp     = TEXT("LieUp");
    
    inline const FString Interact_PickUp    = TEXT("PickUp");
    inline const FString Interact_Drop      = TEXT("Drop");
    inline const FString Interact_Eat       = TEXT("Eat");
    inline const FString Interact_Wear      = TEXT("Wear");
    
    inline const FString Interact_Clean     = TEXT("Clean");
    inline const FString Interact_Repair    = TEXT("Repair");
    inline const FString Interact_Read      = TEXT("Read");
    
    inline const FString Interact_Pray      = TEXT("Pray");
    inline const FString Interact_Dance     = TEXT("Dance");
    inline const FString Interact_Sing      = TEXT("Sing");

    // --- Common Values (일반 값) ---
    inline const FString Value_Neutral      = TEXT("Neutral");
    inline const FString Value_None         = TEXT("None");
    inline const FString Agent_Broadcast    = TEXT("broadcast");
}
