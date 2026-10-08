#pragma once

#include "NativeGameplayTags.h"

// 플레이어 공통 상태 태그
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Idle)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Action_Common_Move)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Action_Combat_Attack)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Condition_Dead)

// VR 자세 태그 — HMD 높이 비율 + 허리 숙이기(피치·높이 비율)로 판정 (Standing/Crouching/Bending/Prone)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Posture_Standing)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Posture_Crouching)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Posture_Prone)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Posture_Bending)
