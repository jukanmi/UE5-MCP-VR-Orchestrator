// File: WebSocketClient.h
// Purpose: C++ Interface for Async WebSocket Communication.
// Manages the connection to the Python Cognitive Engine.
#pragma once

#include "CoreMinimal.h"
#include "WebSocketsModule.h" 
#include "IWebSocket.h"
#include "WebSocketClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketMessage, const FString&, Message);

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UWebSocketClient : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void Initialize(FString ServerURL);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendData(FString JsonData);

    // event dispatchers
    UPROPERTY(BlueprintAssignable, Category = "MCP Network")
    FOnWebSocketMessage OnMessageReceived;

private:
    TSharedPtr<IWebSocket> WebSocket;
    void OnMessage(const FString& Message);
    void OnConnected();
    void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
};
