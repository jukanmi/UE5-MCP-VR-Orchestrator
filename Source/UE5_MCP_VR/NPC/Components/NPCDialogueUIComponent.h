// NPCDialogueUIComponent — NPC 머리 위 말풍선 위젯 + 그 표시 수명 전체.
//
// 위젯 컴포넌트를 상속해 "말풍선 그 자체"가 되게 했다. 자막 텍스트·표시 타이머·
// 카메라 빌보드가 모두 여기 모인다.
// ASmartNPC 는 표시를 시키기만 하고(ShowSubtitle) 수명 관리는 하지 않는다.
//
// 분리 전에는 이 로직이 액터에 있어서, 말풍선이 보이는 동안 액터 전체 틱이 켜지고
// 플린치·넉다운 틱과 수명을 공유했다. 지금은 이 컴포넌트가 자기 틱만 켠다.
#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "NPCDialogueUIComponent.generated.h"

UCLASS(ClassGroup = (MCP), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCDialogueUIComponent : public UWidgetComponent
{
    GENERATED_BODY()

public:
    UNPCDialogueUIComponent();

    /** 머리 위 말풍선에 대사 표시 — 즉시 표시 + 길이 비례 타이머 후 숨김. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void ShowSubtitle(const FString& Text);

    /** 말풍선 숨김 + 텍스트 클리어. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void HideSubtitle();

    /** 기본 노출 시간(초). 텍스트 길이에 비례 가산. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitleFallbackDuration = 4.0f;

    /** 글자당 추가 노출 시간(초) — 긴 대사 더 오래. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitlePerCharDuration = 0.05f;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

private:
    /** 텍스트를 위젯에 반영하고 표시 상태를 지정. 빌보드 틱도 여기서 함께 켜고 끈다.
     *  인자 이름이 bShow 인 이유: bVisible 은 USceneComponent 의 멤버라 가려 버린다. */
    void ApplySubtitle(bool bShow);

    /** 발화자 이름 — 소유 NPC 의 AgentID. 위젯 상단에 표시된다. */
    FString GetSpeakerName() const;

    /** 현재 표시 중 자막 텍스트. */
    FString CurrentSubtitleText;

    FTimerHandle SubtitleHideTimer;
};
