// File: EnvelopeBuilder.cpp
// Role: FEnvelopeBuilder 구현부 - Envelope JSON 직렬화 로직.

#include "EnvelopeBuilder.h"
#include "Json.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "OmniAgentConfig.h"


FString FEnvelopeBuilder::EnvelopeTypeToString(EEnvelopeType Type)
{
    switch (Type)
    {
        case EEnvelopeType::StateUpdate:  return TEXT("state_update");
        case EEnvelopeType::Prompt:       return TEXT("prompt");
        case EEnvelopeType::ActionFailed: return TEXT("action_failed");
        case EEnvelopeType::EmergencyReport: return TEXT("emergency_report");
        case EEnvelopeType::LocationDecision: return TEXT("location_decision");
        default:
            // 새 EEnvelopeType 추가 후 여기 미반영 시 도달(§5 위반 신호) — prompt 폴백은
            // Python 이 엉뚱한 핸들러로 라우팅하는 무음 장애가 되므로 Error 로 승격.
            UE_LOG(LogTemp, Error, TEXT("[EnvelopeBuilder] 알 수 없는 EEnvelopeType(%d) — 'prompt' 폴백. EnvelopeTypeToString 분기 추가 필요(§5)."),
                   static_cast<int32>(Type));
            return TEXT("prompt");
    }
}


FString FEnvelopeBuilder::BuildEnvelope(
    EEnvelopeType  Type,
    const FString& PayloadJson,
    const FString& RefMsgId)
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

    // ref_msg_id는 action_failed에서만 유효, 나머지는 null
    if (!RefMsgId.IsEmpty())
    {
        EnvelopeJson->SetStringField(TEXT("ref_msg_id"), RefMsgId);
    }
    else
    {
        EnvelopeJson->SetField(TEXT("ref_msg_id"), MakeShared<FJsonValueNull>());
    }

    // payload는 이미 직렬화된 JSON 문자열이므로, 역직렬화하여 중첩 삽입
    TSharedPtr<FJsonObject> PayloadObject;
    TSharedRef<TJsonReader<>> PayloadReader = TJsonReaderFactory<>::Create(PayloadJson);

    if (FJsonSerializer::Deserialize(PayloadReader, PayloadObject) && PayloadObject.IsValid())
    {
        EnvelopeJson->SetObjectField(TEXT("payload"), PayloadObject);
    }
    else
    {
        // payload 파싱 실패 시 빈 오브젝트로 안전 폴백
        UE_LOG(LogTemp, Error,
            TEXT("[EnvelopeBuilder] payload JSON 파싱 실패. 빈 payload로 전송합니다. 원본: %s"),
            *PayloadJson);
        EnvelopeJson->SetObjectField(TEXT("payload"), MakeShared<FJsonObject>());
    }

    // ── 최종 직렬화 ────────────────────────────────────────────────────
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(EnvelopeJson, Writer);

    return OutputString;
}



FString FEnvelopeBuilder::BuildPrompt(const FString& PayloadJson)
{
    return BuildEnvelope(EEnvelopeType::Prompt, PayloadJson);
}


FString FEnvelopeBuilder::BuildActionFailed(const FString& RefMsgId, const FString& PayloadJson)
{
    // WHY RefMsgId 필수 검증:
    //   ref_msg_id 없이 action_failed를 보내면 Python이 어떤 명령이 실패했는지
    //   추적할 수 없으므로, 이 값이 비어있으면 즉시 경고를 남긴다.
    if (RefMsgId.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[EnvelopeBuilder] BuildActionFailed 호출 시 RefMsgId가 비어있습니다. "
                 "Python이 실패 이력을 추적하지 못할 수 있습니다."));
    }
    return BuildEnvelope(EEnvelopeType::ActionFailed, PayloadJson, RefMsgId);
}

FString FEnvelopeBuilder::BuildEmergencyReport(const FString& PayloadJson)
{
    return BuildEnvelope(EEnvelopeType::EmergencyReport, PayloadJson);
}

FString FEnvelopeBuilder::BuildLocationDecisionRequest(const FString& PayloadJson)
{
    return BuildEnvelope(EEnvelopeType::LocationDecision, PayloadJson);
}

FString FEnvelopeBuilder::BuildStateUpdate(const FString& PayloadJson)
{
    return BuildEnvelope(EEnvelopeType::StateUpdate, PayloadJson);
}
