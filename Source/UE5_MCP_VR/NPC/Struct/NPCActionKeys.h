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
    inline const FString Key_TargetID   = TEXT("target_id");   // 액션 대상 actor 키워드(Player/Self/Enemy/<NpcName>)
    inline const FString Key_Item       = TEXT("item");        // 아이템 ID (use/equip/give/drop/craft/repair)
    inline const FString Key_Style      = TEXT("style");       // Move=EMoveType 이름(Walk/Run/Sprint/Crouch), Sing/Emote=미디어 키

    // --- Location Keys (위치 정보 키) ---
    inline const FString Key_TargetLoc  = TEXT("target_loc");

    // --- Action Parameter Keys (Parameters 내부 — snake_case 통일, 2026-07-11 결정) ---
    // Key_Direction 은 전투 셀렉터(Dodge 측면 스텝)가 내부 주입. 나머지는 ExecuteInteraction
    // 읽기 전용 예약 키 — Python 송신 미구현(DialogueActionItem 스키마 확장은 수요 확인 후 별건).
    inline const FString Key_Direction     = TEXT("direction");
    inline const FString Key_StartLocation = TEXT("start_location");
    inline const FString Key_EndLocation   = TEXT("end_location");
    inline const FString Key_GiveItemID    = TEXT("give_item_id");
    inline const FString Key_GiveAmount    = TEXT("give_amount");
    inline const FString Key_GetItemID     = TEXT("get_item_id");
    inline const FString Key_GetAmount     = TEXT("get_amount");
    inline const FString Key_Amount        = TEXT("amount");
    inline const FString Key_ItemIDs       = TEXT("item_ids");

    // --- ActionBatch Protocol Keys (최상위 PascalCase — MCPJsonUtils 파싱용) ---
    inline const FString Proto_Mode          = TEXT("Mode");
    inline const FString Proto_Actions       = TEXT("Actions");
    inline const FString Proto_ActionType    = TEXT("ActionType");
    inline const FString Proto_FacialState   = TEXT("FacialState");
    inline const FString Proto_Parameters    = TEXT("Parameters");
    inline const FString Proto_ActionBatches = TEXT("ActionBatches");

    // Python → UE5 비-Envelope 메시지의 타입 식별 키(최상위 "type").
    inline const FString Msg_Type               = TEXT("type");

    // debug_prompt — 브라우저 디버그 대시보드가 친 말을 Python 이 UE5 로 넘길 때 쓰는 타입.
    // UE5 가 이걸 받아 SendPlayerDialogue 를 태우므로 그 뒤는 마이크 경로와 동일하다.
    inline const FString Debug_TypeValue        = TEXT("debug_prompt");
    inline const FString Debug_NpcId            = TEXT("npc_id");
    inline const FString Debug_PlayerId         = TEXT("player_id");
    inline const FString Debug_Text             = TEXT("text");


    // NPCActionComponent에서 애니메이션 몽타주를 재생하기 위해 데이터 에셋과 매핑되는 고유 키워드들입니다.
    // 기존 String 기반 통신에서 발생하던 결합도를 낮추고 오직 몽타주 매핑 용도로 역할을 제한하였습니다.
    inline const FString Interact_SitDown   = TEXT("SitDown");
    inline const FString Interact_SitUp     = TEXT("SitUp");
    inline const FString Interact_LieDown   = TEXT("LieDown");
    inline const FString Interact_LieUp     = TEXT("LieUp");
    
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
