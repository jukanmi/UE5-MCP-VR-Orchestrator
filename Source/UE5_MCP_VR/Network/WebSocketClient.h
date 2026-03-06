#pragma once

#include "CoreMinimal.h"
#include "WebSocketsModule.h" 
#include "IWebSocket.h"
#include "EnvelopeBuilder.h"
#include "WebSocketClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketMessage, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketConnectionChanged, bool, bIsConnected);

// LLM 네트워크 관리를 담당하는 클래스
UCLASS(BlueprintType)
class UE5_MCP_VR_API ULLMNetworkClient : public UObject
{
    GENERATED_BODY()

public:
    void Initialize();
    void SendPrompt(const FString& JsonData);
    void SendStateUpdate(const struct FGameStateData& StateData);
    void Disconnect();
    bool IsConnected() const { return bIsServerConnected; }

    UPROPERTY(BlueprintAssignable)
    FOnWebSocketMessage OnMessageReceived;

    UPROPERTY(BlueprintAssignable)
    FOnWebSocketConnectionChanged OnConnectionChanged;

private:
    UPROPERTY(EditAnywhere, Category = "MCP|Network")
    FString LLMWebSocketURL = TEXT("ws://127.0.0.1:8000/ws/llm");

    UFUNCTION()
    void OnMessageReceivedHandler(const FString& Message);

    UFUNCTION()
    void OnConnectionChangedHandler(bool bIsConnected);

    UPROPERTY()
    class UWebSocketClient* Socket;

    bool bIsServerConnected = false;
};

// SLM 네트워크 관리를 담당하는 클래스
UCLASS(BlueprintType)
class UE5_MCP_VR_API USLMNetworkClient : public UObject
{
    GENERATED_BODY()

public:
    void Initialize();
    void SendPrompt(const FString& JsonData);
    void Disconnect();
    bool IsConnected() const { return bIsServerConnected; }

    UPROPERTY(BlueprintAssignable)
    FOnWebSocketMessage OnMessageReceived;

private:
    UPROPERTY(EditAnywhere, Category = "MCP|Network")
    FString SLMWebSocketURL = TEXT("ws://127.0.0.1:8000/ws/slm");

    UFUNCTION()
    void OnMessageReceivedHandler(const FString& Message);

    UFUNCTION()
    void OnConnectionChangedHandler(bool bIsConnected);

    UPROPERTY()
    class UWebSocketClient* Socket;

    bool bIsServerConnected = false;
};

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UWebSocketClient : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void Initialize(FString ServerURL);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendStateUpdate(const FString& PayloadJson);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendPrompt(const FString& PayloadJson);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendActionFailed(const FString& RefMsgId, const FString& PayloadJson);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendSLMPrompt(const FString& PayloadJson);

    void SendRaw(const FString& RawData);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    bool IsConnected() const;

    UPROPERTY(BlueprintAssignable, Category = "MCP Network")
    FOnWebSocketConnectionChanged OnConnectionChanged;

    UPROPERTY(BlueprintAssignable, Category = "MCP Network")
    FOnWebSocketMessage OnMessageReceived;

private:
    TSharedPtr<IWebSocket> WebSocket;

    FString CachedServerURL;

    int32 RetryCount = 0;

    static constexpr int32 MaxRetryCount = 5;

    FTimerHandle ReconnectTimerHandle;

    void BindSocketEvents();
    void OnMessage(const FString& Message);
    void OnConnected();
    void OnConnectionError(const FString& Error);
    void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);

    void TryReconnect();
};
