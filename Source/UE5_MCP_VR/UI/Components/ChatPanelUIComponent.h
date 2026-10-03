#pragma once

#include "CoreMinimal.h"
#include "UI/Components/WorldUIComponent.h"
#include "ChatPanelUIComponent.generated.h"

/**
 * 채팅 패널 — 예외적으로 카메라에 붙는다(머리 고정). "시야에 고정된 패널은 멀미" 금기는 상시 표시 패널 얘기다.
 * 채팅은 Enter 를 눌렀을 때만 잠깐 켜졌다 닫히므로 그 시간대만 시야에 고정되어도 멀미로 이어지지 않고,
 * 대신 타이핑 중 고개를 돌려도 입력창을 잃지 않는다. 입력 포커스를 잃으면(전송 완료·빈 Enter) 스스로 닫힌다.
 * 클릭 없이 키보드 포커스만 쓰므로 충돌이 필요 없다.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UChatPanelUIComponent : public UWorldUIComponent
{
    GENERATED_BODY()

public:
    UChatPanelUIComponent();

    /** 열고 입력 칸에 포커스. 위젯이 없으면(채팅 위젯 클래스 미지정) 아무것도 안 한다. */
    void Open();

protected:
    virtual void UpdateUI(float DeltaTime) override;
};
