// NPCDialogueUIComponent — NPC 머리 위 말풍선 위젯 + 그 표시 수명 전체.
//
// 위젯 컴포넌트를 상속해 "말풍선 그 자체"가 되게 했다. 자막 텍스트·표시 타이머·
// TTS 음성 싱크·응답 대기(Thinking) 점 애니메이션·카메라 빌보드가 모두 여기 모인다.
// ASmartNPC 는 표시를 시키기만 하고(ShowSubtitle/ShowThinking) 수명 관리는 하지 않는다.
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

    /** 머리 위 말풍선에 대사 표시. bWaitForAudio=true 면 텍스트만 세팅하고 TTS 음성 시작 시 표시,
     *  false 면 즉시 표시 + 폴백 타이머 후 숨김. 동일 발화 재요청은 깜빡임 없이 이어붙임. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void ShowSubtitle(const FString& Text, bool bWaitForAudio);

    /** 말풍선 숨김 + 텍스트 클리어. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void HideSubtitle();

    /** LLM 응답 대기 표시 — 말풍선에 점이 하나씩 늘어난다(. → .. → ...).
     *  응답이 오면 ShowSubtitle 이, 아무것도 안 오면 워치독이 해제한다. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void ShowThinking();

    /** 대기 표시 해제 + 말풍선 숨김. 응답 도착·타임아웃 양쪽에서 호출된다. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void StopThinking();

    /** 지금 응답 대기 중인지. */
    UFUNCTION(BlueprintPure, Category = "MCP|Dialogue")
    bool IsThinking() const { return bThinking; }

    /** TTS 음성 없이 표시할 때 기본 노출 시간(초). 텍스트 길이에 비례 가산. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitleFallbackDuration = 4.0f;

    /** 글자당 추가 노출 시간(초) — 긴 대사 더 오래. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitlePerCharDuration = 0.05f;

    /** 음성 싱크 모드 안전 상한(초) — Completed 누락(스트림 에러·소켓 끊김) 시 강제 숨김. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float SubtitleMaxDuration = 15.0f;

    /** 응답 무한 대기 방지 상한(초). 백엔드가 죽거나 메시지를 흘리면 말풍선이 영원히 남는다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float ThinkingTimeoutSec = 30.f;

    /** 점이 늘어나는 간격(초). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Dialogue")
    float ThinkingDotInterval = 0.35f;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

private:
    /** 텍스트를 위젯에 반영하고 표시 상태를 지정. 빌보드 틱도 여기서 함께 켜고 끈다.
     *  인자 이름이 bShow 인 이유: bVisible 은 USceneComponent 의 멤버라 가려 버린다. */
    void ApplySubtitle(bool bShow);

    /** TTS 음성 시작/완료 델리게이트 바인딩 — 오디오 컴포넌트가 늦게 붙는 경우가 있어 표시할 때마다 재시도. */
    void TryBindAudioSubtitle();

    UFUNCTION()
    void HandleSubtitleAudioStarted();

    UFUNCTION()
    void HandleSubtitleAudioCompleted();

    /** 점 하나 늘려 말풍선에 반영. */
    void TickThinkingDots();

    /** 대기 타이머만 정리(말풍선은 그대로) — 진짜 대사가 뒤이어 올 때 쓴다. */
    void ClearThinkingTimers();

    /** 워치독 만료 — 로그 남기고 해제. */
    void HandleThinkingTimeout();

    /** 발화자 이름 — 소유 NPC 의 AgentID. 위젯 상단에 표시된다. */
    FString GetSpeakerName() const;

    /** 현재 표시/대기 중 자막 텍스트(중복 발화 억제용). */
    FString CurrentSubtitleText;

    bool bSubtitleWaitingForAudio = false;
    bool bAudioSubtitleBound = false;
    FTimerHandle SubtitleHideTimer;

    /** 응답 대기 중 여부 — 점 애니메이션과 워치독이 살아 있다는 뜻. */
    bool bThinking = false;

    /** 지금까지 찍은 점 개수(1~3). */
    int32 ThinkingDotCount = 0;

    FTimerHandle ThinkingDotTimer;
    FTimerHandle ThinkingTimeoutTimer;
};
