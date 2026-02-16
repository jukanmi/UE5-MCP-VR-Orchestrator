#pragma once

#include "CoreMinimal.h"

/**
 * NPCActionKeys
 * 
 * Centralized repository for all string constants used in NPC Action processing.
 * This ensures consistency between JSON parsing, Action dispatching, and Behavior Tree keys.
 */
namespace NPCActionKeys
{
    // --- JSON Keys (JSON 파싱용 키) ---
    inline const FString Key_ActionType = TEXT("action_type");
    inline const FString Key_TargetID   = TEXT("target_id");   
    inline const FString Key_Content    = TEXT("Content"); // Legacy mixed usage
    inline const FString Key_Text       = TEXT("text");
    inline const FString Key_Emotion    = TEXT("emotion");
    inline const FString Key_Params     = TEXT("parameters");

    // --- Location Keys (위치 정보 키) ---
    inline const FString Loc_X          = TEXT("x");
    inline const FString Loc_Y          = TEXT("y");
    inline const FString Loc_Z          = TEXT("z");

    // --- Action Types (액션 타입 정의) ---
    inline const FString Action_Stop        = TEXT("Stop");
    inline const FString Action_Dialogue    = TEXT("Dialogue");
    inline const FString Action_Move        = TEXT("Move");
    inline const FString Action_Wait        = TEXT("Wait");
    inline const FString Action_Attack      = TEXT("Attack");
    inline const FString Action_Block       = TEXT("Block");
    inline const FString Action_Dodge       = TEXT("Dodge");
    inline const FString Action_PickUp      = TEXT("PickUp");
    inline const FString Action_Craft       = TEXT("Craft");
    inline const FString Action_Scan        = TEXT("Scan");
    inline const FString Action_Investigate = TEXT("Investigate");
    inline const FString Action_Sleep       = TEXT("Sleep");
    inline const FString Action_Sit         = TEXT("Sit");
    
    // --- Behavior Modes (행동 모드) ---
    inline const FString Mode_Combat        = TEXT("Combat");
    inline const FString Mode_Task          = TEXT("Task");
    inline const FString Mode_Investigation = TEXT("Investigation");
    inline const FString Mode_Lifestyle     = TEXT("Lifestyle");
    inline const FString Mode_Common        = TEXT("Common");
    
    // --- Common Values (일반 값) ---
    inline const FString Value_Neutral      = TEXT("Neutral");
    inline const FString Value_None         = TEXT("None");
    inline const FString Agent_Broadcast    = TEXT("broadcast");
}
