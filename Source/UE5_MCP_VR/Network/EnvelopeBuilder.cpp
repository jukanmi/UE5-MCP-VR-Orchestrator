// File: EnvelopeBuilder.cpp
// Role: FEnvelopeBuilder 구현부 - Envelope JSON 직렬화 로직.

#include "EnvelopeBuilder.h"
#include "Json.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "OmniAgentConfig.h"
#include "MCPJsonUtils.h"


FString FEnvelopeBuilder::EnvelopeTypeToString(EEnvelopeType Type)
{
    switch (Type)
    {
        case EEnvelopeType::StateUpdate:  return TEXT("state_update");
        case EEnvelopeType::Prompt:       return TEXT("prompt");
        case EEnvelopeType::EmergencyReport: return TEXT("emergency_report");
        case EEnvelopeType::LocationDecision: return TEXT("location_decision");
        default:
            // 새 EEnvelopeType 추가 후 여기 미반영 시 도달(새 Envelope 타입을 양쪽에 동시 반영하지 않았다는 신호) — prompt 폴백은
            // Python 이 엉뚱한 핸들러로 라우팅하는 무음 장애가 되므로 Error 로 승격.
            UE_LOG(LogTemp, Error, TEXT("[EnvelopeBuilder] 알 수 없는 EEnvelopeType(%d) — 'prompt' 폴백. EnvelopeTypeToString 분기 추가 필요."),
                   static_cast<int32>(Type));
            return TEXT("prompt");
    }
}


FString FEnvelopeBuilder::BuildEnvelope(EEnvelopeType Type, const TSharedRef<FJsonObject>& Payload)
{
    // ── 공통 메타데이터 생성 ───────────────────────────────────────────
    // msg_id: 새 GUID 생성 (요청-응답 추적 및 중복 감지용)
    const FString MsgId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

    // timestamp: Unix epoch 초 단위 (Python time.time()과 동일 형식)
    const FDateTime Now = FDateTime::UtcNow();
    const FDateTime Epoch(1970, 1, 1);
    const double UnixTimestamp = (Now - Epoch).GetTotalSeconds();

    // auth_token: Config에서 로드
    const FString AuthToken = FOmniAgentConfig::LoadAuthToken();

    // ── JSON 오브젝트 조립 ─────────────────────────────────────────────
    // WHY FJsonObject: 문자열 직접 조립은 이스케이프 오류 위험이 있으므로
    //     UE5의 공식 JSON API를 사용하여 안전하게 직렬화한다.
    TSharedRef<FJsonObject> EnvelopeJson = MakeShared<FJsonObject>();
    EnvelopeJson->SetNumberField(TEXT("protocol_version"), 1);
    EnvelopeJson->SetStringField(TEXT("msg_id"),     MsgId);
    EnvelopeJson->SetStringField(TEXT("auth_token"), AuthToken);
    EnvelopeJson->SetNumberField(TEXT("timestamp"),  UnixTimestamp);
    EnvelopeJson->SetStringField(TEXT("type"),       EnvelopeTypeToString(Type));

    EnvelopeJson->SetObjectField(TEXT("payload"), Payload);

    return UMCPJsonUtils::ToString(EnvelopeJson);
}



FString FEnvelopeBuilder::BuildPrompt(const TSharedRef<FJsonObject>& Payload)
{
    return BuildEnvelope(EEnvelopeType::Prompt, Payload);
}


FString FEnvelopeBuilder::BuildEmergencyReport(const TSharedRef<FJsonObject>& Payload)
{
    return BuildEnvelope(EEnvelopeType::EmergencyReport, Payload);
}

FString FEnvelopeBuilder::BuildLocationDecisionRequest(const TSharedRef<FJsonObject>& Payload)
{
    return BuildEnvelope(EEnvelopeType::LocationDecision, Payload);
}

FString FEnvelopeBuilder::BuildStateUpdate(const TSharedRef<FJsonObject>& Payload)
{
    return BuildEnvelope(EEnvelopeType::StateUpdate, Payload);
}
