// File: NPCAudioStreamComponent.cpp
// 구현 노트는 헤더 참조. 본 파일은 IWebSocket 청크 → USoundWaveProcedural::QueueAudio 경로만 책임.

#include "NPCAudioStreamComponent.h"

#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"

#include "Struct/NPCActionKeys.h"

UNPCAudioStreamComponent::UNPCAudioStreamComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNPCAudioStreamComponent::PlayFromUrl(const FString& WsUrl, int32 SampleRate, int32 NumChannels)
{
    if (WsUrl.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAudio] PlayFromUrl 호출인데 URL 비어 있음 — TTS 실패 fallback 으로 추정"));
        return;
    }

    // 진행 중인 스트림이 있으면 정리
    Stop();

    ExpectedSampleRate = FMath::Max(SampleRate, 8000);
    ExpectedChannels = FMath::Clamp(NumChannels, 1, 2);

    // SoundWaveProcedural 준비
    ProceduralWave = NewObject<USoundWaveProcedural>(this);
    ProceduralWave->SetSampleRate(ExpectedSampleRate);
    ProceduralWave->NumChannels = ExpectedChannels;
    ProceduralWave->Duration = INDEFINITELY_LOOPING_DURATION;
    ProceduralWave->bLooping = false;
    ProceduralWave->SoundGroup = SOUNDGROUP_Default;

    // AudioComponent 재사용 또는 신규 생성. Actor 의 root 에 attach.
    if (!AudioComponent)
    {
        AudioComponent = NewObject<UAudioComponent>(GetOwner());
        AudioComponent->bAutoActivate = false;
        AudioComponent->bAllowSpatialization = true;
        AudioComponent->RegisterComponent();
        if (USceneComponent* Root = GetOwner() ? GetOwner()->GetRootComponent() : nullptr)
        {
            AudioComponent->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
        }
    }
    AudioComponent->SetSound(ProceduralWave);
    bPlaybackStarted = false;

    // WebSocket 모듈 보장
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    PlayRequestedAt = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Log, TEXT("[NPCAudio][T0] PlayFromUrl 호출 url=%s sr=%d ch=%d"), *WsUrl, ExpectedSampleRate, ExpectedChannels);

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(WsUrl);
    WebSocket->OnConnected().AddUObject(this, &UNPCAudioStreamComponent::HandleConnected);
    WebSocket->OnMessage().AddUObject(this, &UNPCAudioStreamComponent::HandleMessage);
    WebSocket->OnClosed().AddUObject(this, &UNPCAudioStreamComponent::HandleClosed);
    WebSocket->OnConnectionError().AddLambda([](const FString& Err)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAudio] WS 연결 오류: %s"), *Err);
    });
    WebSocket->Connect();
}

void UNPCAudioStreamComponent::HandleConnected()
{
    const double Dt = (FPlatformTime::Seconds() - PlayRequestedAt) * 1000.0;
    UE_LOG(LogTemp, Log, TEXT("[NPCAudio][T1] WS Connected +%.1fms"), Dt);
}

void UNPCAudioStreamComponent::Stop()
{
    if (WebSocket.IsValid())
    {
        WebSocket->OnMessage().Clear();
        WebSocket->OnClosed().Clear();
        if (WebSocket->IsConnected())
        {
            WebSocket->Close();
        }
        WebSocket.Reset();
    }
    if (AudioComponent && AudioComponent->IsPlaying())
    {
        AudioComponent->Stop();
    }
    ResetStreamState();
}

void UNPCAudioStreamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Stop();
    if (AudioComponent)
    {
        AudioComponent->DestroyComponent();
        AudioComponent = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}

void UNPCAudioStreamComponent::HandleMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCAudio] WS 메시지 파싱 실패"));
        return;
    }

    FString MsgType;
    Root->TryGetStringField(TEXT("type"), MsgType);

    if (MsgType == TEXT("audio_chunk"))
    {
        FString B64;
        if (!Root->TryGetStringField(TEXT("audio_base64"), B64) || B64.IsEmpty())
        {
            return;
        }
        TArray<uint8> Pcm;
        if (!FBase64::Decode(B64, Pcm) || Pcm.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("[NPCAudio] Base64 디코딩 실패"));
            return;
        }
        if (ProceduralWave)
        {
            ProceduralWave->QueueAudio(Pcm.GetData(), Pcm.Num());
        }
        if (!bPlaybackStarted && AudioComponent)
        {
            const double Dt = (FPlatformTime::Seconds() - PlayRequestedAt) * 1000.0;
            UE_LOG(LogTemp, Log, TEXT("[NPCAudio][T2] 첫 청크 수신 → Play() +%.1fms (pcm %d bytes)"), Dt, Pcm.Num());
            AudioComponent->Play();
            bPlaybackStarted = true;
            OnAudioStarted.Broadcast();
        }
    }
    else if (MsgType == TEXT("completed"))
    {
        // WebSocket->Close() / Stop() 를 OnMessage 콜백 안에서 직접 부르면
        // WebSocket 모듈의 리스너 broadcast 루프가 도중에 array 변경 → ensure 발생
        // ("Array has changed during ranged-for iteration!"). 다음 게임 틱으로 지연.
        if (UWorld* World = GetWorld())
        {
            TWeakObjectPtr<UNPCAudioStreamComponent> WeakSelf(this);
            World->GetTimerManager().SetTimerForNextTick([WeakSelf]()
            {
                if (UNPCAudioStreamComponent* Self = WeakSelf.Get())
                {
                    if (Self->WebSocket.IsValid() && Self->WebSocket->IsConnected())
                    {
                        Self->WebSocket->Close();
                    }
                    Self->OnAudioCompleted.Broadcast();
                }
            });
        }
    }
    else if (MsgType == TEXT("error"))
    {
        FString Code, Reason;
        Root->TryGetStringField(TEXT("code"), Code);
        Root->TryGetStringField(TEXT("message"), Reason);
        UE_LOG(LogTemp, Warning, TEXT("[NPCAudio] 서버 에러: %s (%s)"), *Code, *Reason);
        // Stop() 도 같은 이유로 지연 — OnMessage broadcast 루프 안에서 delegate 를 Clear 하면 안 됨.
        if (UWorld* World = GetWorld())
        {
            TWeakObjectPtr<UNPCAudioStreamComponent> WeakSelf(this);
            World->GetTimerManager().SetTimerForNextTick([WeakSelf]()
            {
                if (UNPCAudioStreamComponent* Self = WeakSelf.Get())
                {
                    Self->Stop();
                }
            });
        }
    }
}

void UNPCAudioStreamComponent::HandleClosed(int32 StatusCode, const FString& Reason, bool /*bWasClean*/)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCAudio] WS 종료. status=%d reason=%s"), StatusCode, *Reason);
}

void UNPCAudioStreamComponent::ResetStreamState()
{
    bPlaybackStarted = false;
    ProceduralWave = nullptr;
}
