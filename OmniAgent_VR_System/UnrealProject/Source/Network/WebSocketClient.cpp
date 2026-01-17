#include "WebSocketClient.h"
#include "WebSocketsModule.h"

void UWebSocketClient::Initialize(FString ServerURL)
{
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(ServerURL);

    WebSocket->OnConnected().AddUObject(this, &UWebSocketClient::OnConnected);
    WebSocket->OnConnectionError().AddLambda([](const FString& Error) {
        UE_LOG(LogTemp, Error, TEXT("WebSocket Error: %s"), *Error);
    });
    WebSocket->OnClosed().AddUObject(this, &UWebSocketClient::OnClosed);
    WebSocket->OnMessage().AddUObject(this, &UWebSocketClient::OnMessage);

    WebSocket->Connect();
}

void UWebSocketClient::SendData(FString JsonData)
{
    if (WebSocket && WebSocket->IsConnected())
    {
        WebSocket->Send(JsonData);
    }
}

void UWebSocketClient::OnConnected()
{
    UE_LOG(LogTemp, Log, TEXT("WebSocket Connected to Cognitive Engine"));
}

void UWebSocketClient::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogTemp, Warning, TEXT("WebSocket Closed: %s"), *Reason);
}

void UWebSocketClient::OnMessage(const FString& Message)
{
    // TODO: Parse ActionBatch using Unreal's Json Utilities
    UE_LOG(LogTemp, Log, TEXT("Received ActionBatch: %s"), *Message);
}
