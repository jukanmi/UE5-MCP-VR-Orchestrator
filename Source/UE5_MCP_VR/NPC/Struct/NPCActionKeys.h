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
    inline const FString Key_TargetID   = TEXT("target_id");   // Python Parameters 딕셔너리 canonical 키

    // --- Location Keys (위치 정보 키) ---
    inline const FString Key_TargetLoc  = TEXT("target_loc");


    // NPCActionComponent에서 애니메이션 몽타주를 재생하기 위해 데이터 에셋과 매핑되는 고유 키워드들입니다.
    // 기존 String 기반 통신에서 발생하던 결합도를 낮추고 오직 몽타주 매핑 용도로 역할을 제한하였습니다.
    inline const FString Interact_SitDown   = TEXT("SitDown");
    inline const FString Interact_SitUp     = TEXT("SitUp");
    inline const FString Interact_LieDown   = TEXT("LieDown");
    inline const FString Interact_LieUp     = TEXT("LieUp");
    inline const FString Media_Track        = TEXT("Track");
    inline const FString Media_Death        = TEXT("Death");
    
    // --- Noise Volume Config (소음 발생 배율 상수) ---
    inline const float Noise_Dialogue       = 1.0f;     // 일반 대화 소음
    inline const float Noise_Attack         = 1.5f;     // 공격 등 큰 소음
    inline const float Noise_UseItem        = 0.5f;     // 아이템 사용 등 작은 소음
    inline const float Noise_Drop           = 0.8f;     // 물건을 떨어뜨리는 소음

    // --- Noise Tag Config (형식: "EventType:BaseDanger") ---
    inline const FName NoiseTag_Dialogue    = FName("Dialogue:0.1");
    inline const FName NoiseTag_Attack      = FName("Attack:0.9");
    inline const FName NoiseTag_UseItem     = FName("UseItem:0.2");
    inline const FName NoiseTag_Drop        = FName("Drop:0.4");
}
