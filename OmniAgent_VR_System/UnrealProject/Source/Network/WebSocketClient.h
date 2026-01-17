#pragma once

#include "CoreMinimal.h"
#include "WebSocketsModule.h" 
#include "IWebSocket.h"
#include "WebSocketClient.generated.h"

UCLASS()
class UNREALPROJECT_API UWebSocketClient : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(FString ServerURL);
    void SendData(FString JsonData);

    // Event delegates could go here (OnMessageReceived, etc.)

private:
    TSharedPtr<IWebSocket> WebSocket;
    void OnMessage(const FString& Message);
    void OnConnected();
    void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
};
