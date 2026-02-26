// File: EnvelopeBuilder.cpp
// Role: FEnvelopeBuilder 구현부 - Envelope JSON 직렬화 로직.

#include "EnvelopeBuilder.h"
#include "Json.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "Misc/ConfigCacheIni.h"

// ─────────────────────────────────────────────────────────────────────────────
// WHY 정적 변수로 토큰을 캐시:
//   Config 파일 I/O는 비용이 있으므로, 서버 시작 시 1회만 읽고
//   이후에는 메모리의 캐시된 값을 사용한다.
// ─────────────────────────────────────────────────────────────────────────────
static FString CachedAuthToken;
static bool    bAuthTokenLoaded = false;


FString FEnvelopeBuilder::LoadAuthToken()
{
    // 이미 로드했으면 캐시 반환
    if (bAuthTokenLoaded)
    {
        return CachedAuthToken;
    }

    // Config/DefaultGame.ini의 [OmniAgent] 섹션에서 AuthToken을 읽는다.
    // WHY INI 파일: 소스코드 하드코딩을 방지하고, 환경별(Dev/Prod) 토큰을 분리 관리한다.
    GConfig->GetString(
        TEXT("OmniAgent"),      // 섹션명
        TEXT("AuthToken"),      // 키명
        CachedAuthToken,
        GGameIni                // DefaultGame.ini
    );

    if (CachedAuthToken.IsEmpty())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[EnvelopeBuilder] Config/DefaultGame.ini에 [OmniAgent] AuthToken이 설정되지 않았습니다. "
                 "Python 서버의 .env WS_AUTH_TOKEN과 일치하는 값을 추가하세요."));
        CachedAuthToken = TEXT(""); // 빈 문자열 → Python에서 인증 거부됨
    }

    bAuthTokenLoaded = true;
    return CachedAuthToken;
}


FString FEnvelopeBuilder::EnvelopeTypeToString(EEnvelopeType Type)
{
    switch (Type)
    {
        case EEnvelopeType::StateUpdate:  return TEXT("state_update");
        case EEnvelopeType::Prompt:       return TEXT("prompt");
        case EEnvelopeType::ActionFailed: return TEXT("action_failed");
        default:
            UE_LOG(LogTemp, Warning, TEXT("[EnvelopeBuilder] 알 수 없는 EEnvelopeType. 'prompt'으로 폴백."));
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
    const FString AuthToken = LoadAuthToken();

    // ── JSON 오브젝트 조립 ─────────────────────────────────────────────
    // WHY FJsonObject: 문자열 직접 조립은 이스케이프 오류 위험이 있으므로
    //     UE5의 공식 JSON API를 사용하여 안전하게 직렬화한다.
    TSharedRef<FJsonObject> EnvelopeJson = MakeShared<FJsonObject>();
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


FString FEnvelopeBuilder::BuildStateUpdate(const FString& PayloadJson)
{
    return BuildEnvelope(EEnvelopeType::StateUpdate, PayloadJson);
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
