#include "WebSocketClient.h"
#include "WebSocketsModule.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "MCPJsonUtils.h"
// --- UWebSocketClient ---

void UWebSocketClient::Initialize(FString ServerURL)
{
    // TODO: [UE5] Python 서버 연결 시 보안을 위해 ServerURL에 "?auth_token={토큰}" 쿼리 파라미터를 추가하거나,
    // FWebSocketsModule::Get().CreateWebSocket 호출 시 Header에 JWT를 포함시키도록 변경하세요.

    CachedServerURL = ServerURL;

    RetryCount = 0;

    WebSocket = FModuleManager::Get().LoadModuleChecked<FWebSocketsModule>("WebSockets").CreateWebSocket(ServerURL, TEXT("ws"));

    if (WebSocket.IsValid())
    {
        BindSocketEvents();
        WebSocket->Connect();
    }
}

void UWebSocketClient::BindSocketEvents()
{
    WebSocket->OnConnected().AddUObject(this, &UWebSocketClient::OnConnected);
    WebSocket->OnConnectionError().AddUObject(this, &UWebSocketClient::OnConnectionError);
    WebSocket->OnClosed().AddUObject(this, &UWebSocketClient::OnClosed);
    WebSocket->OnMessage().AddUObject(this, &UWebSocketClient::OnMessage);
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

void UNetworkClientBase::Initialize(const FString& InURL)
{
    TargetURL = InURL;
    Socket = NewObject<UWebSocketClient>(this);
    if (Socket)
    {
        Socket->OnMessageReceived.AddDynamic(this, &UNetworkClientBase::OnMessageReceivedHandler);
        Socket->OnConnectionChanged.AddDynamic(this, &UNetworkClientBase::OnConnectionChangedHandler);
        Socket->Initialize(TargetURL);
    }
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

void USLMNetworkClient::OnMessageReceivedHandler(const FString& Message)
{
    OnMessageReceived.Broadcast(Message);
}
void USLMNetworkClient::OnConnectionChangedHandler(bool bIsConnected)
{
    bIsServerConnected = bIsConnected;
}

