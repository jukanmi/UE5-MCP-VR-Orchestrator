// VoiceInputComponent — Push-to-talk 마이크 캡처 → ASR 서비스(WS) 스트리밍 → transcript.
//
// 흐름:
//   StartTalking() → ASR WS(ws://…/ws/asr/stream) 연결 → start(JSON) 송신 → 마이크 캡처 시작
//   → 오디오 콜백이 float→int16 mono 로 큐잉 → Tick 이 게임스레드에서 바이너리 청크 전송
//   StopTalking() → 캡처 중지 → 잔여 flush → end(JSON) → 서버가 final(transcript) 회신
//   → HandleAsrMessage 가 OnTranscriptReady 브로드캐스트 → 폰이 SendPlayerDialogue 로 전달.
//
// 리샘플 정책: UE 는 마이크 네이티브 레이트(보통 48kHz) 그대로 전송하고 start.sample_rate 에
// 명시한다. 16kHz 리샘플은 Python(ASR) 책임. (CLAUDE.md §0 결정)
#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "VoiceInputComponent.generated.h"

class IWebSocket;

UCLASS(ClassGroup = (MCP), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UVoiceInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoiceInputComponent();

    /** Push-to-talk 누름 — WS 연결·start 송신·마이크 캡처 시작. */
    UFUNCTION(BlueprintCallable, Category = "ASR")
    void StartTalking();

    /** Push-to-talk 뗌 — 캡처 중지·end 송신. final 은 비동기로 도착. */
    UFUNCTION(BlueprintCallable, Category = "ASR")
    void StopTalking();

    UFUNCTION(BlueprintPure, Category = "ASR")
    bool IsTalking() const { return bTalking; }

    /** ASR 스트리밍 WS 엔드포인트. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASR")
    FString AsrServerURL = TEXT("ws://127.0.0.1:8002/ws/asr/stream");

    /** 인식 언어 힌트 (start.language). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASR")
    FString Language = TEXT("KR");

    /** 디버그용 — true 이면 StopTalking 시 Saved/VoiceCapture.wav 저장. 테스트 후 끌 것. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ASR|Debug")
    bool bSaveDebugWav = false;

    /** transcript 확정 시 호출 — (PlayerId, TargetNpcId, Transcript). 폰이 바인딩. */
    DECLARE_DELEGATE_ThreeParams(FOnTranscriptReady, const FString&, const FString&, const FString&);
    FOnTranscriptReady OnTranscriptReady;

    /** 발화 대상 NPC / 플레이어 ID 공급자 — 소유 폰이 세팅(없으면 빈 문자열). */
    TFunction<FString()> ResolveTargetNpc;
    TFunction<FString()> ResolvePlayerId;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // 마이크 캡처 (저수준 — raw float 콜백). 오디오 스레드에서 HandleAudioGenerate 호출.
    Audio::FAudioCapture AudioCapture;

    TSharedPtr<IWebSocket> Socket;

    bool bTalking = false;
    bool bStartSent = false;
    // 오디오 스레드(HandleAudioGenerate 직전 콜백)에서 쓰고 게임 스레드(SendStartIfReady)에서 읽음.
    // 동기화 없으면 ARM64(Quest)에서 데이터 레이스 → atomic 으로 보호.
    std::atomic<int32> StreamSampleRate{ 48000 };
    FString RequestId;
    FString PendingTargetNpc;
    FString PendingPlayerId;

    // 오디오 스레드 → 게임 스레드 핸드오프 (int16 mono 누적)
    FCriticalSection PcmLock;
    TArray<int16> PcmQueue;

    // 오디오 스레드 콜백 — 인터리브 float 를 mono int16 으로 다운믹스해 PcmQueue 에 누적.
    void HandleAudioGenerate(const float* InAudio, int32 NumFrames, int32 NumChannels);
    void FlushPcmToSocket();
    void SendStartIfReady();
    void HandleAsrMessage(const FString& Message);
    void CloseSocket();

    /** 마이크 캡처 스트림 정지·해제 — 열려있을 때만. StopTalking·EndPlay 공용. */
    void StopAudioCaptureStream();

    void SaveDebugWav(const TArray<int16>& Pcm, int32 SampleRate);

    // bSaveDebugWav 시 전체 PCM 누적 버퍼 (PcmLock 으로 보호)
    TArray<int16> DebugPcmBuffer;
};
