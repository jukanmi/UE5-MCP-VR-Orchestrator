#pragma once

#include "Engine/EngineTypes.h"

// 플레이어 손 물리 바디 전용 오브젝트 채널(DefaultEngine.ini 의 HandLeft/HandRight). 기본 응답이 Block 이라 벽·물체는 그대로 손을 막는다.
constexpr ECollisionChannel ECC_HandLeft  = ECC_GameTraceChannel1;
constexpr ECollisionChannel ECC_HandRight = ECC_GameTraceChannel2;
