#include "MCPJsonUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"

namespace
{
    TSharedPtr<FJsonObject> ConvertLocationToJson(const FVector& Location)
    {
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetNumberField(TEXT("x"), FMath::RoundToFloat(Location.X * 100.f) / 100.f);
        LocObj->SetNumberField(TEXT("y"), FMath::RoundToFloat(Location.Y * 100.f) / 100.f);
        LocObj->SetNumberField(TEXT("z"), FMath::RoundToFloat(Location.Z * 100.f) / 100.f);
        return LocObj;
    }

    TSharedPtr<FJsonObject> ConvertPerceptionToJson(const FPerceptionData& Target)
    {
        TSharedPtr<FJsonObject> TargetObj = MakeShared<FJsonObject>();
        TargetObj->SetStringField(TEXT("target_id"), Target.TargetID);
        
        FString SenseStr = TEXT("Other");
        if (const UEnum* SenseEnum = StaticEnum<ESenseType>())
        {
            SenseStr = SenseEnum->GetNameStringByValue(static_cast<int64>(Target.SenseType));
        }

        TargetObj->SetStringField(TEXT("sense_type"), SenseStr);
        TargetObj->SetNumberField(TEXT("distance"), FMath::RoundToFloat(Target.Distance));
        TargetObj->SetObjectField(TEXT("location"), ConvertLocationToJson(Target.Location));
        
        return TargetObj;
    }
}

/**
 * FGameStateData를 Python 백엔드가 읽을 수 있는 state_update JSON으로 직렬화합니다.
 */
FString UMCPJsonUtils::SerializeGameState(const FGameStateData& StateData)
{
    TSharedPtr<FJsonObject> PayloadObject = MakeShared<FJsonObject>();
    
    PayloadObject->SetStringField(TEXT("owner_agent_id"), StateData.OwnerAgentID);
    PayloadObject->SetStringField(TEXT("threat_level"), StateData.ThreatLevel);
    PayloadObject->SetBoolField(TEXT("in_cover"), StateData.bIsInCover);
    PayloadObject->SetBoolField(TEXT("line_of_sight"), StateData.bHasLineOfSight);
    PayloadObject->SetObjectField(TEXT("owner_location"), ConvertLocationToJson(StateData.OwnerLocation));

    TArray<TSharedPtr<FJsonValue>> PerceptionArray;
    for (const FPerceptionData& Target : StateData.PerceivedTargets)
    {
        PerceptionArray.Add(MakeShared<FJsonValueObject>(ConvertPerceptionToJson(Target)));
    }
    PayloadObject->SetArrayField(TEXT("perceived_targets"), PerceptionArray);

    FString OutputJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
    if (!FJsonSerializer::Serialize(PayloadObject.ToSharedRef(), Writer))
    {
        UE_LOG(LogTemp, Error, TEXT("[MCPJsonUtils] Failed to serialize GameStateData Payload to JSON."));
        return FString();
    }
    
    return OutputJson;
}




namespace
{
    // [의도(Why)] 문자열 형태의 Mode 필드 값을 타입 안전한 Enum(ENPCBehaviorMode)으로 변환합니다.
    bool TryParseBehaviorMode(const TSharedPtr<FJsonObject>& JsonObj, const FString& FieldName, ENPCBehaviorMode& OutMode)
    {
        return TryParseEnumFromJson(JsonObj, FieldName, OutMode);
    }

    // [의도(Why)] 액션의 파라미터가 중첩된 JsonObject를 가질 경우, 데이터 손실 방지를 위해 문자열로 재직렬화 후 저장합니다.
    void ExtractActionParameters(TSharedPtr<FJsonObject> ActionObj, TMap<FString, FString>& OutParameters)
    {
        const TSharedPtr<FJsonObject>* ParamsObj;
        if (ActionObj->TryGetObjectField(TEXT("Parameters"), ParamsObj) || 
            ActionObj->TryGetObjectField(TEXT("parameters"), ParamsObj))
        {
            for (const auto& ParamPair : (*ParamsObj)->Values)
            {
                if (ParamPair.Value->Type == EJson::Object)
                {
                    FString NestedString;
                    TSharedRef<TJsonWriter<>> NestedWriter = TJsonWriterFactory<>::Create(&NestedString);
                    FJsonSerializer::Serialize(ParamPair.Value->AsObject().ToSharedRef(), NestedWriter);
                    OutParameters.Add(ParamPair.Key, NestedString);
                }
                else
                {
                    OutParameters.Add(ParamPair.Key, ParamPair.Value->AsString());
                }
            }
        }
    }

    // [의도(Why)] Enum Type 직렬화 도우미 함수 템플릿 (보일러플레이트 제거)
    template<typename TEnum>
    bool TryParseEnumFromJson(const TSharedPtr<FJsonObject>& JsonObj, const FString& FieldName, TEnum& OutEnum)
    {
        FString EnumString;
        // PascalCase 및 snake_case 모두 지원
        if (JsonObj->TryGetStringField(FieldName, EnumString) || 
            JsonObj->TryGetStringField(FieldName.ToLower(), EnumString))
        {
            if (const UEnum* EnumPtr = StaticEnum<TEnum>())
            {
                int64 EnumValue = EnumPtr->GetValueByNameString(EnumString);
                if (EnumValue != INDEX_NONE)
                {
                    OutEnum = static_cast<TEnum>(EnumValue);
                    return true;
                }
            }
        }
        return false;
    }

    // [의도(Why)] 단일 Action 단위 정보 객체를 파싱하여 GameAction 열거형 파생 타입 및 하위 매개변수를 추출합니다.
    bool TryExtractGameAction(TSharedPtr<FJsonObject> ActionObj, FGameAction& OutAction)
    {
        if (!ActionObj.IsValid()) return false;

        TryParseEnumFromJson(ActionObj, TEXT("ActionType"), OutAction.ActionType);

        TryParseEnumFromJson(ActionObj, TEXT("FacialState"), OutAction.FacialState);

        ExtractActionParameters(ActionObj, OutAction.Parameters);
        return true;
    }

    // [의도(Why)] 에이전트 단위로 부여받은 ActionBatch 정보를 추출하여 큐에 들어갈 단일 액션 리스트들을 구성합니다.
    bool TryExtractActionBatch(const FString& AgentID, TSharedPtr<FJsonObject> BatchObj, FActionBatch& OutBatch)
    {
        if (!BatchObj.IsValid()) return false;

        OutBatch.AgentID = AgentID;
        
        TryParseBehaviorMode(BatchObj, TEXT("Mode"), OutBatch.Mode);

        const TArray<TSharedPtr<FJsonValue>>* ActionsArray;
        if (BatchObj->TryGetArrayField(TEXT("Actions"), ActionsArray))
        {
            for (const TSharedPtr<FJsonValue>& Val : *ActionsArray)
            {
                FGameAction NewAction;
                if (TryExtractGameAction(Val->AsObject(), NewAction))
                {
                    OutBatch.Actions.Add(NewAction);
                }
            }
        }
        return true;
    }
}

bool UMCPJsonUtils::ParseModeActionRequest(FString Json, FModeActionRequest& OutRequest)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return false;
    }

    // "ActionBatches" 속성으로 유효한 FModeActionRequest 포맷인지 판별
    if (!RootObject->HasField(TEXT("ActionBatches"))) 
    {
        return false;
    }

    TryParseBehaviorMode(RootObject, TEXT("Mode"), OutRequest.Mode);

    TSharedPtr<FJsonObject> BatchesObj = RootObject->GetObjectField(TEXT("ActionBatches"));
    if (BatchesObj.IsValid())
    {
        for (const auto& Pair : BatchesObj->Values)
        {
            FActionBatch NewBatch;
            if (TryExtractActionBatch(Pair.Key, Pair.Value->AsObject(), NewBatch))
            {
                OutRequest.ActionBatches.Add(Pair.Key, NewBatch);
            }
        }
    }

    return true;
}
