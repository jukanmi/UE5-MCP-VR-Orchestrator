#pragma once

#include "CoreMinimal.h"
#include "UI/Components/WorldUIComponent.h"
#include "ItemTooltipUIComponent.generated.h"

class ADroppedItemBase;
struct FItemData;

/**
 * 아이템 이름표 — 이름 + 종류·수량(+ 진열 가격). 카메라 정면을 향한다.
 * 두 가지로 쓴다:
 *  - 따라가는 이름표(플레이어): ShowForItem 을 매 틱 불러 그 아이템 위에 띄운다. 아이템마다 위젯을 달면 개수만큼
 *    틱이 늘어나므로 하나를 대상만 바꿔 옮겨 쓴다.
 *  - 고정 안내(거래 요구 품목): 붙은 자리에서 SetItem 으로 내용만 채운다.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UItemTooltipUIComponent : public UWorldUIComponent
{
    GENERATED_BODY()

public:
    UItemTooltipUIComponent();

    /** 아이템 위로 띄우는 높이(cm) — ShowForItem 용. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    float HeightOffset = 15.f;

    /** 내용만 채운다(위치는 그대로). */
    void SetItem(const FItemData& Data, int32 Amount, int32 Price = -1);

    /** 그 아이템 위에 띄운다. 대상이 바뀔 때만 텍스트를 다시 만든다(매 틱 SetText 는 폰트 셰이핑을 다시 돌려
     *  VR 90Hz 에서 프레임을 갉아먹는다). 마스터 테이블에 없는 ID 면 띄우지 않는다 — 빈 상자만 뜨는 게 더 헷갈린다. */
    void ShowForItem(ADroppedItemBase* Item);

    /** 숨기고 대상을 잊는다. */
    void Clear();

private:
    /** 직전에 이름표를 그린 아이템. */
    UPROPERTY(Transient)
    TObjectPtr<ADroppedItemBase> Target;
};
