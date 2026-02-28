#pragma once

#include "CoreMinimal.h"

/**
 * NPCActionKeys
 * 
 * NPC 행동(Action) 처리 시 사용되는 문자열 상수들을 중앙 집중화합니다.
 * 하드코딩된 문자열로 인한 오타 방지 및 유지보수성 향상을 목적으로 합니다.
 * 현재는 JSON 파싱, 디버그 데이터 생성, 핵심 상호작용(Interaction) 몽타주 매핑에 제한적으로 사용됩니다.
 */
namespace NPCActionKeys
{
    // --- JSON Keys (JSON 파싱용 키) ---
    inline const FString Key_Text       = TEXT("text");

    // --- Location Keys (위치 정보 키) ---
    // 좌표는 "target_loc": {"x":0, "y":0, "z":0} 형태로 한 세트로 전달됨
    inline const FString Key_TargetLoc  = TEXT("target_loc");
    inline const FString Loc_X          = TEXT("x");
    inline const FString Loc_Y          = TEXT("y");
    inline const FString Loc_Z          = TEXT("z");


    
    // NPCActionComponent에서 애니메이션 몽타주를 재생하기 위해 데이터 에셋과 매핑되는 고유 키워드들입니다.
    // 기존 String 기반 통신에서 발생하던 결합도를 낮추고 오직 몽타주 매핑 용도로 역할을 제한하였습니다.
    inline const FString Interact_SitDown   = TEXT("SitDown");
    inline const FString Interact_SitUp     = TEXT("SitUp");
    inline const FString Interact_LieDown   = TEXT("LieDown");
    inline const FString Interact_LieUp     = TEXT("LieUp");
    // --- Common Values (일반 값) ---
    inline const FString Value_None         = TEXT("None");
    inline const FString Agent_Broadcast    = TEXT("broadcast");
}
