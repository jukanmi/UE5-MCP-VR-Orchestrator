#include "VoiceInputComponent.h"

#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UVoiceInputComponent::UVoiceInputComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false; // 말할 때만 켬
}

void UVoiceInputComponent::StartTalking()
{
    if (bTalking) return;

    // 빠른 재누름 가드 — 이전 소켓이 final 대기로 남아있을 수 있어 명시 정리(고스트 연결/누수 방지).
    CloseSocket();

    bTalking = true;
    bStartSent = false;
    RequestId = FString::Printf(TEXT("asr_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12));

    // 대상/플레이어 해석 (폰 공급자)
    PendingTargetNpc = ResolveTargetNpc ? ResolveTargetNpc() : FString();
    PendingPlayerId  = ResolvePlayerId  ? ResolvePlayerId()  : GetOwner() ? GetOwner()->GetName() : TEXT("Player");

    {
        FScopeLock Lock(&PcmLock);
        PcmQueue.Reset();
        DebugPcmBuffer.Reset();
    }

    // 1) WS 연결
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }
    Socket = FWebSocketsModule::Get().CreateWebSocket(AsrServerURL);
    if (!Socket.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Voice] WS 생성 실패 — %s"), *AsrServerURL);
        bTalking = false;
        return;
    }

    // 비동기 WS 콜백 — 컴포넌트가 먼저 소멸되면 raw this 는 use-after-free.
    // TWeakObjectPtr 로 매 콜백에서 생존 검증 후 실행.
    TWeakObjectPtr<UVoiceInputComponent> WeakThis(this);
    Socket->OnConnected().AddLambda([WeakThis]()
    {
        if (UVoiceInputComponent* StrongThis = WeakThis.Get())
        {
            if (!StrongThis->bTalking)
            {
                // 연결 완료 전에 이미 뗀 빠른 탭 — start 없이 즉시 정리
                UE_LOG(LogTemp, Log, TEXT("[Voice] ASR WS 연결됨 — 이미 발화 종료(빠른 탭), 소켓 정리"));
                StrongThis->CloseSocket();
                return;
            }
            UE_LOG(LogTemp, Log, TEXT("[Voice] ASR WS 연결됨 — start 송신"));
            StrongThis->SendStartIfReady();
        }
    });
    Socket->OnConnectionError().AddLambda([WeakThis](const FString& Error)
    {
        if (UVoiceInputComponent* StrongThis = WeakThis.Get())
        {
            UE_LOG(LogTemp, Warning, TEXT("[Voice] ASR WS 연결오류 — %s"), *Error);
            StrongThis->bTalking = false;
            if (StrongThis->AudioCapture.IsStreamOpen())
            {
                StrongThis->AudioCapture.StopStream();
                StrongThis->AudioCapture.CloseStream();
            }
            StrongThis->SetComponentTickEnabled(false);
        }
    });
    Socket->OnMessage().AddLambda([WeakThis](const FString& Msg)
    {
        if (UVoiceInputComponent* StrongThis = WeakThis.Get())
        {
            StrongThis->HandleAsrMessage(Msg);
        }
    });
    // 연결 수립 후 서버 측 종료 감지 — OnConnectionError 는 연결 실패 전용이라
    // 미바인딩 시 발화 중 서버 사망이 무음 폐기됨(transcript 안 오는 원인 추적 불가).
    // CloseSocket() 이 핸들러를 먼저 비우므로 여기 도달 = 의도치 않은 원격 종료.
    Socket->OnClosed().AddLambda([WeakThis](int32 StatusCode, const FString& Reason, bool bWasClean)
    {
        if (UVoiceInputComponent* StrongThis = WeakThis.Get())
        {
            UE_LOG(LogTemp, Warning, TEXT("[Voice] ASR WS 원격 종료 (code=%d clean=%d reason=%s) — 발화 폐기·캡처 정리"),
                StatusCode, bWasClean ? 1 : 0, *Reason);
            StrongThis->bTalking = false;
            if (StrongThis->AudioCapture.IsStreamOpen())
            {
                StrongThis->AudioCapture.StopStream();
                StrongThis->AudioCapture.CloseStream();
            }
            StrongThis->SetComponentTickEnabled(false);
            StrongThis->bStartSent = false;
            // Socket.Reset() 은 콜백 내 자기파괴 위험 — 다음 StartTalking 의 CloseSocket 이 정리.
        }
    });
    Socket->Connect();

    // 2) 마이크 캡처 오픈 — 콜백에서 raw float 수신
    // 디바이스 샘플레이트를 미리 조회해 start.sample_rate 정확도 확보(첫 콜백 전 start 송신 대비)
    Audio::FCaptureDeviceInfo DevInfo;
    if (AudioCapture.GetCaptureDeviceInfo(DevInfo) && DevInfo.PreferredSampleRate > 0)
    {
        StreamSampleRate = DevInfo.PreferredSampleRate;
        UE_LOG(LogTemp, Log, TEXT("[Voice] 캡처 장치: %s  sr=%d  ch=%d"),
            *DevInfo.DeviceName, DevInfo.PreferredSampleRate, DevInfo.InputChannels);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Voice] 캡처 장치 정보 조회 실패 — 기본값 사용"));
    }

    Audio::FAudioCaptureDeviceParams Params;
    Audio::FOnAudioCaptureFunction OnCapture =
        [this](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 InSampleRate, double /*StreamTime*/, bool /*bOverflow*/)
    {
        StreamSampleRate = InSampleRate;
        HandleAudioGenerate(static_cast<const float*>(InAudio), NumFrames, NumChannels);
    };

    if (AudioCapture.OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024))
    {
        AudioCapture.StartStream();
        SetComponentTickEnabled(true);
    }
    else
    {
        // 마이크 실패 시 토킹 상태·소켓 정리 — 방치하면 bTalking=true 로 굳어
        // 다음 StartTalking 이 조기 리턴하고, 연결 중 소켓이 고스트로 남는다.
        UE_LOG(LogTemp, Warning, TEXT("[Voice] 마이크 캡처 스트림 오픈 실패 — 음성 입력 중단"));
        bTalking = false;
        CloseSocket();
    }
}

void UVoiceInputComponent::StopTalking()
{
    if (!bTalking) return;
    bTalking = false;

    if (AudioCapture.IsStreamOpen())
    {
        AudioCapture.StopStream();
        AudioCapture.CloseStream();
    }

    // 잔여 PCM flush 후 end 송신
    FlushPcmToSocket();
    if (Socket.IsValid() && Socket->IsConnected())
    {
        Socket->Send(TEXT("{\"type\":\"end\"}"));
        UE_LOG(LogTemp, Log, TEXT("[Voice] end 송신 — final 대기"));
    }

    SetComponentTickEnabled(false);

    // 디버그 WAV 저장 (bSaveDebugWav 켜져있을 때만)
    if (bSaveDebugWav)
    {
        TArray<int16> CaptureCopy;
        {
            FScopeLock Lock(&PcmLock);
            CaptureCopy = DebugPcmBuffer;
        }
        SaveDebugWav(CaptureCopy, StreamSampleRate.load());
    }

    // 소켓은 final 수신까지 열어둠 — HandleAsrMessage(final) 가 CloseSocket 호출.
}

void UVoiceInputComponent::SendStartIfReady()
{
    // !bTalking 가드: 연결 완료 전에 이미 뗀 빠른 탭 — start 만 보내고 오디오·end 없이
    // 서버 세션을 타임아웃까지 고스트로 남기는 것 방지.
    if (!bTalking || bStartSent || !Socket.IsValid() || !Socket->IsConnected()) return;
    bStartSent = true;

    const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("type"), TEXT("start"));
    Obj->SetStringField(TEXT("request_id"), RequestId);
    Obj->SetStringField(TEXT("target_npc_id"), PendingTargetNpc);
    Obj->SetStringField(TEXT("player_id"), PendingPlayerId);
    Obj->SetNumberField(TEXT("sample_rate"), StreamSampleRate.load());
    Obj->SetStringField(TEXT("language"), Language);

    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Obj, Writer);
    Socket->Send(Out);
}

void UVoiceInputComponent::HandleAudioGenerate(const float* InAudio, int32 NumFrames, int32 NumChannels)
{
    if (NumFrames <= 0 || NumChannels <= 0 || !InAudio) return;

    FScopeLock Lock(&PcmLock);
    PcmQueue.Reserve(PcmQueue.Num() + NumFrames);
    for (int32 Frame = 0; Frame < NumFrames; ++Frame)
    {
        // 다중 채널 → mono 평균
        float Sum = 0.f;
        for (int32 Ch = 0; Ch < NumChannels; ++Ch)
        {
            Sum += InAudio[Frame * NumChannels + Ch];
        }
        const float Mono = Sum / NumChannels;
        const int32 S = FMath::RoundToInt(FMath::Clamp(Mono, -1.f, 1.f) * 32767.f);
        const int16 Sample = static_cast<int16>(S);
        PcmQueue.Add(Sample);
        if (bSaveDebugWav) DebugPcmBuffer.Add(Sample);
    }
}

void UVoiceInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    SendStartIfReady();   // 연결이 늦으면 여기서 start 보장
    FlushPcmToSocket();
}

void UVoiceInputComponent::FlushPcmToSocket()
{
    if (!Socket.IsValid() || !Socket->IsConnected() || !bStartSent) return;

    TArray<int16> ToSend;
    {
        FScopeLock Lock(&PcmLock);
        if (PcmQueue.Num() == 0) return;
        ToSend = MoveTemp(PcmQueue);
        PcmQueue.Reset();
    }
    // s16le 바이너리 전송 (little-endian 가정 — Win64)
    Socket->Send(ToSend.GetData(), ToSend.Num() * sizeof(int16), /*bIsBinary=*/true);
}

void UVoiceInputComponent::HandleAsrMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> Obj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return;

    // 누락/비문자열 키에서 GetStringField 는 어설션/크래시 — TryGetStringField 로 안전 처리.
    FString Type;
    if (!Obj->TryGetStringField(TEXT("type"), Type)) return;
    if (Type == TEXT("final"))
    {
        FString Transcript;
        Obj->TryGetStringField(TEXT("transcript"), Transcript);
        FString Target = PendingTargetNpc;
        Obj->TryGetStringField(TEXT("target_npc_id"), Target);
        FString Player = PendingPlayerId;
        Obj->TryGetStringField(TEXT("player_id"), Player);

        UE_LOG(LogTemp, Log, TEXT("[Voice] final transcript=\"%s\" target=%s"), *Transcript, *Target);
        if (!Transcript.IsEmpty() && OnTranscriptReady.IsBound())
        {
            OnTranscriptReady.Execute(Player, Target, Transcript);
        }
        CloseSocket();
    }
    else if (Type == TEXT("error"))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Voice] ASR error — %s"), *Message);
        CloseSocket();
    }
    // "ready" / "partial" 등은 현재 무시 (partial 은 M2 자막용)
}

void UVoiceInputComponent::CloseSocket()
{
    if (Socket.IsValid())
    {
        // 핸들러 선해제 — 우리가 닫는 소켓의 OnClosed 가 "원격 종료" 경고로 오인되는 것 방지.
        Socket->OnConnected().Clear();
        Socket->OnConnectionError().Clear();
        Socket->OnMessage().Clear();
        Socket->OnClosed().Clear();
        if (Socket->IsConnected()) Socket->Close();
        Socket.Reset();
    }
    bStartSent = false;
}

void UVoiceInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AudioCapture.IsStreamOpen())
    {
        AudioCapture.StopStream();
        AudioCapture.CloseStream();
    }
    CloseSocket();
    Super::EndPlay(EndPlayReason);
}

void UVoiceInputComponent::SaveDebugWav(const TArray<int16>& Pcm, int32 SampleRate)
{
    if (Pcm.IsEmpty()) return;

    const int32 DataSize = Pcm.Num() * sizeof(int16);

    TArray<uint8> Wav;
    Wav.Reserve(44 + DataSize);

    auto W4 = [&](const char* S) { Wav.Append(reinterpret_cast<const uint8*>(S), 4); };
    auto WU32 = [&](uint32 V)    { Wav.Append(reinterpret_cast<const uint8*>(&V), 4); };
    auto WU16 = [&](uint16 V)    { Wav.Append(reinterpret_cast<const uint8*>(&V), 2); };

    W4("RIFF");
    WU32(36 + DataSize);         // ChunkSize
    W4("WAVE");
    W4("fmt ");
    WU32(16);                    // Subchunk1Size (PCM)
    WU16(1);                     // AudioFormat: PCM
    WU16(1);                     // NumChannels: mono
    WU32(static_cast<uint32>(SampleRate));
    WU32(static_cast<uint32>(SampleRate) * 2); // ByteRate
    WU16(2);                     // BlockAlign
    WU16(16);                    // BitsPerSample
    W4("data");
    WU32(DataSize);
    Wav.Append(reinterpret_cast<const uint8*>(Pcm.GetData()), DataSize);

    const FString Path = FPaths::ProjectSavedDir() / TEXT("VoiceCapture.wav");
    FFileHelper::SaveArrayToFile(Wav, *Path);
    UE_LOG(LogTemp, Log, TEXT("[Voice] 디버그 WAV 저장: %s  (%d samples @ %dHz, %.1fs)"),
        *Path, Pcm.Num(), SampleRate, static_cast<float>(Pcm.Num()) / SampleRate);
}
