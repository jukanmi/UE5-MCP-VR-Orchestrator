#include "WebSocketClient.h"
#include "WebSocketsModule.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "MCPJsonUtils.h"

// --- ULLMNetworkClient ---

void ULLMNetworkClient::Initialize()
{
    Socket = NewObject<UWebSocketClient>(this);
    if (Socket)
    {
        Socket->OnMessageReceived.AddDynamic(this, &ULLMNetworkClient::OnMessageReceivedHandler);
        Socket->OnConnectionChanged.AddDynamic(this, &ULLMNetworkClient::OnConnectionChangedHandler);
        Socket->Initialize(LLMWebSocketURL);
        UE_LOG(LogTemp, Log, TEXT("[LLMNetworkClient] Successfully Bound to LLM WebSocket Pipeline URL: %s"), *LLMWebSocketURL);
    }
}

void ULLMNetworkClient::Disconnect()
{
    if (Socket)
    {
        Socket->Disconnect(); // Assuming UWebSocketClient has a Disconnect method, otherwise just let it be GC'd or add later.
    }
    bIsServerConnected = false;
}

void ULLMNetworkClient::SendPrompt(const FString& JsonData)
{
    if (bIsServerConnected && Socket)
    {
        Socket->SendPrompt(JsonData);
    }
}

void ULLMNetworkClient::SendStateUpdate(const FGameStateData& StateData)
{
    if (!bIsServerConnected || !Socket)
    {
        return;
    }

    FString PayloadJson = UMCPJsonUtils::SerializeGameState(StateData);
    if (PayloadJson.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[LLMNetworkClient] SendStateUpdate: Payload Json serialization failed."));
        return;
    }
    
    FString FinalEnvelopeJson = FEnvelopeBuilder::BuildStateUpdate(PayloadJson);
    if (FinalEnvelopeJson.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[LLMNetworkClient] SendStateUpdate: Final Envelope Json serialization failed."));
        return;
    }
    Socket->SendStateUpdate(FinalEnvelopeJson);
}

void ULLMNetworkClient::OnMessageReceivedHandler(const FString& Message)
{
    OnMessageReceived.Broadcast(Message);
}

void ULLMNetworkClient::OnConnectionChangedHandler(bool bIsConnected)
{
    bIsServerConnected = bIsConnected;
    OnConnectionChanged.Broadcast(bIsConnected);
}

// --- USLMNetworkClient ---

void USLMNetworkClient::Initialize()
{
    Socket = NewObject<UWebSocketClient>(this);
    if (Socket)
    {
        Socket->OnMessageReceived.AddDynamic(this, &USLMNetworkClient::OnMessageReceivedHandler);
        Socket->OnConnectionChanged.AddDynamic(this, &USLMNetworkClient::OnConnectionChangedHandler);
        Socket->Initialize(SLMWebSocketURL);
        UE_LOG(LogTemp, Log, TEXT("[SLMNetworkClient] SLM WebSocket initialized. URL: %s"), *SLMWebSocketURL);
    }
}

void USLMNetworkClient::Disconnect()
{
    if (Socket)
    {
        Socket->Disconnect();
    }
    bIsServerConnected = false;
}

void USLMNetworkClient::SendPrompt(const FString& JsonData)
{
    if (!Socket)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SLMNetworkClient] SendPrompt: SLM socket is not initialized."));
        return;
    }
    Socket->SendSLMPrompt(JsonData);
}

void USLMNetworkClient::OnConnectionChangedHandler(bool bIsConnected)
{
    bIsServerConnected = bIsConnected;
}

void USLMNetworkClient::OnMessageReceivedHandler(const FString& Message)
{
    OnMessageReceived.Broadcast(Message);
}

// --- UWebSocketClient ---

void UWebSocketClient::Initialize(FString ServerURL)
{
    // TODO: [UE5] Python 서버 연결 시 보안을 위해 ServerURL에 "?auth_token={토큰}" 쿼리 파라미터를 추가하거나,
    // FWebSocketsModule::Get().CreateWebSocket 호출 시 Header에 JWT를 포함시키도록 변경하세요.

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

// ─────────────────────────────────────────────────────────────────────────────
void UWebSocketClient::SendSLMPrompt(const FString& PayloadJson)
{
    SendRaw(PayloadJson);
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
    RetryCount = 0;
    UE_LOG(LogTemp, Log, TEXT("[WebSocketClient] Successfully connected to Cognitive Engine."));
    OnConnectionChanged.Broadcast(true);
}

void UWebSocketClient::OnConnectionError(const FString& Error)
{
    UE_LOG(LogTemp, Error, TEXT("[WebSocketClient] Connection Error: %s"), *Error);
    OnConnectionChanged.Broadcast(false);
    TryReconnect();
}

void UWebSocketClient::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogTemp, Warning, TEXT("[WebSocketClient] Connection Closed. Code=%d, Reason=%s"), StatusCode, *Reason);
    OnConnectionChanged.Broadcast(false);
    TryReconnect();
}

void UWebSocketClient::TryReconnect()
{
    if (RetryCount >= MaxRetryCount)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[WebSocketClient] Max reconnect attempts (%d) exceeded. Switching to permanent Offline AI Mode."),
            MaxRetryCount);
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(ReconnectTimerHandle);
        }
        return;
    }

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
                WebSocket = FWebSocketsModule::Get().CreateWebSocket(CachedServerURL);
                BindSocketEvents();
                WebSocket->Connect();
            },
            Delay,
            false
        );
    }
}

void UWebSocketClient::OnMessage(const FString& Message)
{
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
