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

    FString PayloadJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadJson);
    FJsonSerializer::Serialize(Payload, Writer);

    const FString Envelope = FEnvelopeBuilder::BuildStateUpdate(PayloadJson);
    SendPrompt(Envelope);
}


