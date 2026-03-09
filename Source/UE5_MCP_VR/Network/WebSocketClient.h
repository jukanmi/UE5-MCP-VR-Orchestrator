#pragma once

#include "CoreMinimal.h"
#include "WebSocketsModule.h" 
#include "IWebSocket.h"
#include "EnvelopeBuilder.h"
#include "WebSocketClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketMessage, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketConnectionChanged, bool, bIsConnected);

// 기초 웹소켓 통신을 담당하는 클래스
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UWebSocketClient : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void Initialize(FString ServerURL);

    // 여러 Send 함수들을 하나로 통합
    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendMessage(const FString& PayloadJson){if (IsConnected()) WebSocket->Send(PayloadJson);}

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    bool IsConnected() const {return WebSocket.IsValid() && WebSocket->IsConnected();}

    void Disconnect(){if (IsConnected()) WebSocket->Close();}
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
    void OnMessage(const FString& Message){OnMessageReceived.Broadcast(Message);}
    void OnConnected(){RetryCount = 0; OnConnectionChanged.Broadcast(true);}
    void OnConnectionError(const FString& Error){OnConnectionChanged.Broadcast(false); TryReconnect();}
    void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean){OnConnectionChanged.Broadcast(false); TryReconnect();}

    void TryReconnect();
    
};

UCLASS(Abstract, BlueprintType)
class UE5_MCP_VR_API UNetworkClientBase : public UObject
{
    GENERATED_BODY()

public:
    virtual void Initialize(const FString& InURL);

    virtual void SendPrompt(const FString& JsonData)
    {
        if (Socket && bIsServerConnected)
            Socket->SendMessage(JsonData);
    }

    void Disconnect()
    {
        if (Socket) Socket->Disconnect();
        bIsServerConnected = false;
    }

    bool IsConnected() const { return bIsServerConnected; }

    UPROPERTY(BlueprintAssignable)
    FOnWebSocketMessage OnMessageReceived;

    UPROPERTY(BlueprintAssignable)
    FOnWebSocketConnectionChanged OnConnectionChanged;

protected:
    UPROPERTY()
    class UWebSocketClient* Socket;

    bool bIsServerConnected = false;
    FString TargetURL;

    UFUNCTION()
    virtual void OnMessageReceivedHandler(const FString& Message)
    {
        OnMessageReceived.Broadcast(Message);
    }

    UFUNCTION()
    virtual void OnConnectionChangedHandler(bool bIsConnected)
    {
        bIsServerConnected = bIsConnected;
        OnConnectionChanged.Broadcast(bIsConnected);
    }
};

// LLM 네트워크 관리
UCLASS(BlueprintType)
class UE5_MCP_VR_API ULLMNetworkClient : public UNetworkClientBase
{
    GENERATED_BODY()

public:
    void InitializeLLM()
    {
        Super::Initialize(LLMWebSocketURL);
    }

    // LLM 전용 추가 기능이 필요하다면 여기에 작성
    void SendStateUpdate(const struct FGameStateData& StateData);

private:
    UPROPERTY(EditAnywhere, Category = "MCP|Network")
    FString LLMWebSocketURL = TEXT("ws://127.0.0.1:8000/ws/llm");
};

// SLM 네트워크 관리
UCLASS(BlueprintType)
class UE5_MCP_VR_API USLMNetworkClient : public UNetworkClientBase
{
    GENERATED_BODY()

public:
    void InitializeSLM()
    {
        Super::Initialize(SLMWebSocketURL);
    }

private:
    UPROPERTY(EditAnywhere, Category = "MCP|Network")
    FString SLMWebSocketURL = TEXT("ws://127.0.0.1:8000/ws/slm");
};