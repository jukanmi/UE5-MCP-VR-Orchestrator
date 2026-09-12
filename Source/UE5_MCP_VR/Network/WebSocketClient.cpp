#include "WebSocketClient.h"
#include "WebSocketsModule.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "MCPJsonUtils.h"
#include "JsonObjectConverter.h"
#include "EnvelopeBuilder.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
// --- UWebSocketClient ---

void UWebSocketClient::Initialize(FString ServerURL)
{
    // 인증은 MessageEnvelope에 포함된 auth_token으로 처리됨 — URL/헤더 JWT 주입 불필요.
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

void UWebSocketClient::UnbindSocketEvents()
{
    if (!WebSocket.IsValid()) return;
    WebSocket->OnConnected().Clear();
    WebSocket->OnConnectionError().Clear();
    WebSocket->OnClosed().Clear();
    WebSocket->OnMessage().Clear();
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
        TWeakObjectPtr<UWebSocketClient> WeakThis(this);
        World->GetTimerManager().SetTimer(
            ReconnectTimerHandle,
            [this, WeakThis]()
            {
                if (!WeakThis.IsValid()) return;   // 재연결 타이머 발화 시 객체 GC 가드
                // 구 소켓 이벤트 해제 후 Close — 미해제 시 Close 가 OnClosed 콜백을 발화시켜
                // TryReconnect 재진입 → 방금 만든 새 소켓을 버리고 또 생성(재연결 폭주)
                UnbindSocketEvents();
                if (WebSocket.IsValid() && WebSocket->IsConnected())
                    WebSocket->Close();
                WebSocket = FWebSocketsModule::Get().CreateWebSocket(CachedServerURL);
                BindSocketEvents();
                WebSocket->Connect();
            },
            Delay,
            false
        );
    }
}

void ULLMNetworkClient::SendStateUpdate(const FGameStateData& StateData)
{
    // Python StateUpdatePayload 스키마에 맞춰 snake_case로 직접 조립.
    // FJsonObjectConverter는 camelCase를 만들어 Python과 호환되지 않음.
    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("owner_agent_id"), StateData.OwnerAgentID);

    const UEnum* ModeEnum = StaticEnum<ENPCBehaviorMode>();
    const FString ModeStr = ModeEnum ? ModeEnum->GetNameStringByValue(static_cast<int64>(StateData.CurrentMode)) : TEXT("Common");
    Payload->SetStringField(TEXT("current_mode"), ModeStr);

    TSharedRef<FJsonObject> LocObj = MakeShared<FJsonObject>();
    LocObj->SetNumberField(TEXT("x"), StateData.OwnerLocation.X);
    LocObj->SetNumberField(TEXT("y"), StateData.OwnerLocation.Y);
    LocObj->SetNumberField(TEXT("z"), StateData.OwnerLocation.Z);
    Payload->SetObjectField(TEXT("owner_location"), LocObj);

    Payload->SetStringField(TEXT("threat_level"), StateData.ThreatLevel);
    Payload->SetBoolField(TEXT("in_cover"), StateData.bIsInCover);
    Payload->SetBoolField(TEXT("line_of_sight"), StateData.bHasLineOfSight);

    // perceived_targets는 비어있어도 OK (기본값 빈 배열)
    Payload->SetArrayField(TEXT("perceived_targets"), TArray<TSharedPtr<FJsonValue>>{});

    SendMessage(FEnvelopeBuilder::BuildStateUpdate(UMCPJsonUtils::ToString(Payload)));
}


