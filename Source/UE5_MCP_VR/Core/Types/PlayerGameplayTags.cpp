#include "Core/Types/PlayerGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_State_Idle, "State.Idle")
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Action_Common_Move, "State.Action.Common.Move")
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Action_Combat_Attack, "State.Action.Combat.Attack")
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Condition_Dead, "State.Condition.Dead")

UE_DEFINE_GAMEPLAY_TAG(TAG_State_Posture_Standing,  "State.Posture.Standing")
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Posture_Crouching, "State.Posture.Crouching")
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Posture_Prone,     "State.Posture.Prone")

//TODO: 다른 상태 태그를 추가할건데 어떤자세인지 어떻게 결정할지 고민중
// 구상으로는 관절 각도가지고 판단 모델을 만들지 고민중 (매우 소형)