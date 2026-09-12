#pragma once

#include "CoreMinimal.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "EnvelopeBuilder.h"
#include "OmniAgentConfig.h"
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

    // 여러 Send 함수들을 하나로 통합. 미연결 시 메시지는 버려짐 — 경고 로그로 가시화.
    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendMessage(const FString& PayloadJson)
    {
        if (IsConnected())
        {
            WebSocket->Send(PayloadJson);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[WebSocketClient] 미연결 상태 송신 시도 — 메시지 유실 (%d bytes)"), PayloadJson.Len());
        }
    }

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

    /** 구 소켓의 모든 이벤트 바인딩 해제 — 재연결 시 stale 콜백 발화 방지. */
    void UnbindSocketEvents();
    void OnMessage(const FString& Message){OnMessageReceived.Broadcast(Message);}
    void OnConnected(){RetryCount = 0; OnConnectionChanged.Broadcast(true);}
    void OnConnectionError(const FString& Error){OnConnectionChanged.Broadcast(false); TryReconnect();}
    void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean){OnConnectionChanged.Broadcast(false); TryReconnect();}

    void TryReconnect();
    
};

// LLM 채널 클라이언트 — 소켓 그 자체다. 예전엔 소켓을 멤버로 든 Abstract 중간 클래스가 델리게이트를
// 같은 이름으로 재브로드캐스트했는데, /ws/slm 채널이 사라진 뒤로는 자식이 하나뿐이라 한 겹 걷어냈다.
UCLASS(BlueprintType)
class UE5_MCP_VR_API ULLMNetworkClient : public UWebSocketClient
{
    GENERATED_BODY()

public:
    void InitializeLLM()
    {
        // 서버 주소는 Config/DefaultGame.ini [OmniAgent] 에서 읽는다(하드코딩 금지).
        Initialize(FOmniAgentConfig::GetLLMWebSocketURL());
    }

    /** state_update Envelope 조립·송신 — Python StateUpdatePayload 스키마(snake_case). */
    void SendStateUpdate(const struct FGameStateData& StateData);
};

