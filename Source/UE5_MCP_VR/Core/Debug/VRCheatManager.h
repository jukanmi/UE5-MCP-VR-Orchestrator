#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "VRCheatManager.generated.h"

class AVRPawn;

/**
 * VR 플레이어 디버그·튜닝 콘솔 명령. 폰에서 분리했다 — 콘솔 Exec 는 컴포넌트에 전달되지 않고
 * PlayerController·폰·HUD·GameMode·CheatManager 에만 가므로 치트 매니저가 이 용도의 표준 자리다.
 * Shipping 빌드엔 치트 매니저가 만들어지지 않는다. 플레이어 컨트롤러의 CheatClass 로 지정해 쓴다.
 */
UCLASS()
class UE5_MCP_VR_API UVRCheatManager : public UCheatManager
{
    GENERATED_BODY()

public:
    /** 손 장비 해제. 인자 0 = 오른손 무기, 1 = 왼손 방패. */
    UFUNCTION(Exec)
    void Cheat_Unequip(bool bOffHand);

    /** 쥔 아이템의 손안 자세를 델타로 밀어보고 절대값을 CSV 표기로 찍는다. 예: TuneGrab 0 0 1 0 15 0
     *  델타가 전부 0 이면 현재 값만 출력(조회). 찍힌 값을 DT_ItemRegistry 의 HoldOffset/HoldRotation 에 붙여넣어 확정. */
    UFUNCTION(Exec)
    void TuneGrab(float DX, float DY, float DZ, float DPitch, float DYaw, float DRoll);

    /** 인벤토리 실제 내용 + HUD 위젯 연결 상태 덤프. */
    UFUNCTION(Exec)
    void DumpInventoryHUD();

    /** 인벤토리 패널 열기/닫기 토글(에디터 디버그용). */
    UFUNCTION(Exec)
    void ToggleInventory();

private:
    AVRPawn* GetVRPawn() const;
};
