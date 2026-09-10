// File: NPCAudioStreamComponent.h
// Role: NPC 음성 스트림 수신·재생 컴포넌트 (TTS 오디오 재생).
//
// WHY:
//   - Orchestrator 가 보낸 NpcAudioResponse 의 audio_stream.url 로
//     별도 WebSocket 을 열어 pcm_s16le 청크를 받아 USoundWaveProcedural 로 재생.
//   - 액션 응답(ActionBatch) 와 오디오 파이프라인의 생명주기 분리.
//
// USAGE (Blueprint 또는 디스패쳐 C++):
//   - NPC Actor 에 본 컴포넌트를 추가 (사용자가 BP 에디터에서 첨부)
//   - 외부에서 PlayFromUrl(Url, SampleRate, Channels) 호출
//
// NOTE:
//   - 본 컴포넌트는 Blackboard 를 쓰지 않는다 (BB 쓰기는 AI 컨트롤러만). 재생 상태가 BT 에
//     영향을 줘야 하면 SmartNPCAIController 가 본 컴포넌트의 델리게이트를 구독해 BB 를 쓴다.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCAudioStreamComponent.generated.h"

class IWebSocket;
class UAudioComponent;
class USoundWaveProcedural;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNpcAudioStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNpcAudioCompleted);

UCLASS(ClassGroup = (MCP), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCAudioStreamComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPCAudioStreamComponent();

    /** TTS WebSocket URL 에 연결해 pcm_s16le 청크를 받고 즉시 재생을 시작한다. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Audio")
    void PlayFromUrl(const FString& WsUrl, int32 SampleRate = 16000, int32 NumChannels = 1);

    /** 진행 중인 스트림을 중단하고 자원을 정리. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Audio")
    void Stop();

    UPROPERTY(BlueprintAssignable, Category = "MCP|Audio")
    FOnNpcAudioStarted OnAudioStarted;

    UPROPERTY(BlueprintAssignable, Category = "MCP|Audio")
    FOnNpcAudioCompleted OnAudioCompleted;

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void HandleMessage(const FString& Message);
    void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void HandleConnected();
    void ResetStreamState();

    // 지연 측정용 타임스탬프 (FPlatformTime::Seconds 기준)
    double PlayRequestedAt = 0.0;

    TSharedPtr<IWebSocket> WebSocket;

    UPROPERTY()
    UAudioComponent* AudioComponent;

    UPROPERTY()
    USoundWaveProcedural* ProceduralWave;

    int32 ExpectedSampleRate = 16000;
    int32 ExpectedChannels = 1;
    bool bPlaybackStarted = false;
};
