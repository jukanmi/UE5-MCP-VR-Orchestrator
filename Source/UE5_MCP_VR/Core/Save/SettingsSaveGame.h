#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SettingsSaveGame.generated.h"

/**
 * 플레이어 설정 저장 슬롯 — 메뉴 설정 화면의 값을 로컬에 두고 다음 실행부터 자동 적용한다.
 * 항목이 늘면(신체 측정 결과 등) 필드만 추가한다. 이전 저장 파일은 없는 필드가 기본값으로 채워진다.
 */
UCLASS()
class UE5_MCP_VR_API USettingsSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    /** 저장 슬롯 이름. */
    static const FString SlotName;

    /** 마스터 볼륨(선형 0~1). 마스터 서브믹스 출력 볼륨에 그대로 적용한다. */
    UPROPERTY()
    float MasterVolume = 1.f;
};
