// File: WebSocketClient.h
// Purpose: C++ Interface for Async WebSocket Communication.
// Manages the connection to the Python Cognitive Engine.
#pragma once

#include "CoreMinimal.h"
#include "WebSocketsModule.h" 
#include "IWebSocket.h"
#include "EnvelopeBuilder.h"
#include "WebSocketClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketMessage, const FString&, Message);
// 연결 상태 변화를 NPCManager 등 구독자에게 알리기 위한 델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebSocketConnectionChanged, bool, bIsConnected);

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UWebSocketClient : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void Initialize(FString ServerURL);

    // ─────────────────────────────────────────────────────────────────────
    // Envelope 기반 전송 인터페이스
    // ─────────────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendStateUpdate(const FString& PayloadJson);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendPrompt(const FString& PayloadJson);

    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    void SendActionFailed(const FString& RefMsgId, const FString& PayloadJson);

    // 원시 문자열 전송 (내부적으로 Envelope 직렬화 후 사용)
    void SendRaw(const FString& RawData);

    // WebSocket이 현재 활성 연결 상태인지 외부에서 조회할 수 있는 함수
    UFUNCTION(BlueprintCallable, Category = "MCP Network")
    bool IsConnected() const;

    // 연결 해제 시 NPCManager가 Blackboard의 IsConnected를 갱신할 수 있도록 알림
    UPROPERTY(BlueprintAssignable, Category = "MCP Network")
    FOnWebSocketConnectionChanged OnConnectionChanged;

    UPROPERTY(BlueprintAssignable, Category = "MCP Network")
    FOnWebSocketMessage OnMessageReceived;

private:
    TSharedPtr<IWebSocket> WebSocket;

    // 재연결 시도를 위한 서버 URL 보존
    FString CachedServerURL;

    // 현재까지 시도한 재연결 횟수 (최대 MaxRetryCount 제한)
    int32 RetryCount = 0;

    // 최대 재시도 횟수: 초과 시 완전 Offline Mode로 고정
    static constexpr int32 MaxRetryCount = 5;

    // Exponential Backoff 기반 재연결 지연 타이머 핸들
    FTimerHandle ReconnectTimerHandle;

    void BindSocketEvents();
    void OnMessage(const FString& Message);
    void OnConnected();
    void OnConnectionError(const FString& Error);
    void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);

    // 재연결 시도: 실패할수록 대기 시간이 두 배씩 늘어나고(Backoff), 한계 초과 시 포기합니다.
    void TryReconnect();
};
