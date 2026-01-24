// File: WebSocketClient.cpp
// Purpose: Implementation of WebSocket logic.
// Uses FWebSocketsModule to connect, send GesPrompt JSON, and receive ActionBatch JSON.
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
        UE_LOG(LogTemp, Log, TEXT("Sent Data: %s"), *JsonData);
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
    UE_LOG(LogTemp, Log, TEXT("Received ActionBatch: %s"), *Message);
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
