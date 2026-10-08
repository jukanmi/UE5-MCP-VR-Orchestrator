#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "BodyMeasureSaveGame.generated.h"

/**
 * 신체 측정 저장 슬롯 — 설정 슬롯(USettingsSaveGame)과 분리해, 측정만 지우거나 다시 쓸 수 있게 한다.
 * 모든 길이는 바닥 기준 cm. 키는 HMD(눈) 높이라 자세 판정의 기준 높이로 그대로 쓴다.
 * bValid 가 false 면 측정한 적 없는 것 — 시작 시 자동 캘리브레이션으로 폴백한다.
 */
UCLASS()
class UE5_MCP_VR_API UBodyMeasureSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    /** 저장 슬롯 이름. */
    static const FString SlotName;

    /** 측정을 마치고 값 검증까지 통과한 저장본인가. */
    UPROPERTY()
    bool bValid = false;

    /** 키 = 선 자세 HMD 높이(cm). */
    UPROPERTY()
    float Height = 0.f;

    /** 팔 벌림 = T자세 양손 수평 간격(cm). */
    UPROPERTY()
    float ArmSpan = 0.f;

    /** 무릎 높이(cm). */
    UPROPERTY()
    float KneeHeight = 0.f;

    /** 엉덩이(골반 옆) 높이(cm). */
    UPROPERTY()
    float HipHeight = 0.f;
};
