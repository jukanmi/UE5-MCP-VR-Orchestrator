// File: WebSocketClient.cpp
// Purpose: Implementation of WebSocket logic with Exponential Backoff Reconnect.
// Uses FWebSocketsModule to connect, send GesPrompt JSON, and receive ActionBatch JSON.
#include "WebSocketClient.h"
#include "WebSocketsModule.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UWebSocketClient::Initialize(FString ServerURL)
{
    // TODO: [UE5] Python 서버 연결 시 보안을 위해 ServerURL에 "?auth_token={토큰}" 쿼리 파라미터를 추가하거나,
    // FWebSocketsModule::Get().CreateWebSocket 호출 시 Header에 JWT를 포함시키도록 변경하세요.

    // 재연결 시도 시 사용하기 위해 URL을 캐싱합니다.
    CachedServerURL = ServerURL;

    RetryCount = 0;

    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(CachedServerURL);
    BindSocketEvents();
    WebSocket->Connect();
}

void UWebSocketClient::BindSocketEvents()
{
    // 소켓 이벤트 바인딩을 별도 함수로 분리해 재연결 시에도 재활용할 수 있도록 합니다.
    WebSocket->OnConnected().AddUObject(this, &UWebSocketClient::OnConnected);
    WebSocket->OnConnectionError().AddUObject(this, &UWebSocketClient::OnConnectionError);
    WebSocket->OnClosed().AddUObject(this, &UWebSocketClient::OnClosed);
    WebSocket->OnMessage().AddUObject(this, &UWebSocketClient::OnMessage);
}

void UWebSocketClient::SendStateUpdate(const FString& PayloadJson)
{
    FString Envelope = FEnvelopeBuilder::BuildStateUpdate(PayloadJson);
    SendRaw(Envelope);
}

void UWebSocketClient::SendPrompt(const FString& PayloadJson)
{
    FString Envelope = FEnvelopeBuilder::BuildPrompt(PayloadJson);
    SendRaw(Envelope);
}

void UWebSocketClient::SendActionFailed(const FString& RefMsgId, const FString& PayloadJson)
{
    FString Envelope = FEnvelopeBuilder::BuildActionFailed(RefMsgId, PayloadJson);
    SendRaw(Envelope);
}

void UWebSocketClient::SendRaw(const FString& RawData)
{
    if (WebSocket && WebSocket->IsConnected())
    {
        WebSocket->Send(RawData);
        UE_LOG(LogTemp, Log, TEXT("[WebSocketClient] Sent Data (%d bytes)"), RawData.Len());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[WebSocketClient] SendRaw skipped: socket is not connected."));
    }
}

bool UWebSocketClient::IsConnected() const
{
    return WebSocket.IsValid() && WebSocket->IsConnected();
}

void UWebSocketClient::OnConnected()
{
    // 연결 성공 시 재시도 카운트를 초기화하고, 구독자(NPCManager 등)에 연결 성공을 알립니다.
    RetryCount = 0;
    UE_LOG(LogTemp, Log, TEXT("[WebSocketClient] Successfully connected to Cognitive Engine."));
    OnConnectionChanged.Broadcast(true);
}

void UWebSocketClient::OnConnectionError(const FString& Error)
{
    UE_LOG(LogTemp, Error, TEXT("[WebSocketClient] Connection Error: %s"), *Error);
    // 연결 오류 발생 시 Offline Fallback 전환을 알리고 재연결을 시도합니다.
    OnConnectionChanged.Broadcast(false);
    TryReconnect();
}

void UWebSocketClient::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogTemp, Warning, TEXT("[WebSocketClient] Connection Closed. Code=%d, Reason=%s"), StatusCode, *Reason);
    // 연결 해제 시 NPCManager가 Blackboard의 IsConnected 키를 False로 바꿀 수 있도록 알립니다.
    OnConnectionChanged.Broadcast(false);
    TryReconnect();
}

void UWebSocketClient::TryReconnect()
{
    // 최대 재시도(MaxRetryCount: 5회)를 초과한 경우 완전 Offline AI Mode로 고정합니다.
    if (RetryCount >= MaxRetryCount)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[WebSocketClient] Max reconnect attempts (%d) exceeded. Switching to permanent Offline AI Mode."),
            MaxRetryCount);
        // 이미 OnConnectionChanged(false)가 공지된 상태이므로, 타이머만 정리하고 종료합니다.
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(ReconnectTimerHandle);
        }
        return;
    }

    // Exponential Backoff: 2^RetryCount 초 후 재시도 (1초 < 2초 < 4초 < 8초 < 16초)
    const float Delay = FMath::Pow(2.f, static_cast<float>(RetryCount));
    RetryCount++;

    UE_LOG(LogTemp, Log,
        TEXT("[WebSocketClient] Retrying connection in %.1f seconds... (Attempt %d/%d)"),
        Delay, RetryCount, MaxRetryCount);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            ReconnectTimerHandle,
            [this]()
            {
                // 새로운 소켓 인스턴스를 생성하고 이벤트를 다시 바인딩한 뒤 연결합니다.
                WebSocket = FWebSocketsModule::Get().CreateWebSocket(CachedServerURL);
                BindSocketEvents();
                WebSocket->Connect();
            },
            Delay,
            false // 단발성 타이머 (루프 아님)
        );
    }
}

void UWebSocketClient::OnMessage(const FString& Message)
{
    // 렌더 스레드에서 호출될 경우 Game Thread로 안전하게 마샬링합니다.
    if (IsInGameThread())
    {
        OnMessageReceived.Broadcast(Message);
    }
    else
    {
        AsyncTask(ENamedThreads::GameThread, [this, Message]()
        {
            OnMessageReceived.Broadcast(Message);
        });
    }
}
