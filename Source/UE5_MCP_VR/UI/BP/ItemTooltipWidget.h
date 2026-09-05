// ItemTooltipWidget — 월드에 떨어진 아이템 위에 뜨는 이름표.
//
// WBP 없이 C++ 가 위젯 트리를 직접 만든다. 디자이너에서 만들 것이 텍스트 두 줄뿐이라
// 에셋을 하나 늘려 에디터 작업을 만드는 것보다 여기서 짓는 편이 싸다.
// TextRenderComponent 를 쓰지 않는 이유: 그쪽은 UFont 한 장만 보고 폴백이 없어
// 한글 표시명이 전부 두부(□)가 된다. UMG 는 슬레이트 폰트 폴백을 탄다.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/BP/ItemDataAsset.h"
#include "ItemTooltipWidget.generated.h"

class UTextBlock;
class UBorder;

UCLASS()
class UE5_MCP_VR_API UItemTooltipWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 표시 내용 갱신 — 이름 + 종류·수량. 종류에 따라 이름 색이 바뀐다. */
    UFUNCTION(BlueprintCallable, Category = "Tooltip")
    void SetItem(const FItemData& Data, int32 Amount);

protected:
    /** 위젯 트리를 코드로 구성 — 디자이너 트리가 없을 때만 짓는다(WBP 로 파생해도 안 깨지게). */
    virtual TSharedRef<SWidget> RebuildWidget() override;

    UPROPERTY(Transient)
    UTextBlock* NameText = nullptr;

    UPROPERTY(Transient)
    UTextBlock* DetailText = nullptr;
};
