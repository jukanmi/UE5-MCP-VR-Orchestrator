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

    FString SenseTypeToString(ESenseType Sense)
    {
        if (const UEnum* EnumPtr = StaticEnum<ESenseType>())
        {
            return EnumPtr->GetNameStringByValue(static_cast<int64>(Sense));
        }
        return TEXT("None");
    }
}




namespace
{
    template<typename TEnum>
    bool TryParseEnumFromJson(const TSharedPtr<FJsonObject>& JsonObj, const FString& FieldName, TEnum& OutEnum)
    {
        FString EnumString;
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

    bool TryParseBehaviorMode(const TSharedPtr<FJsonObject>& JsonObj, const FString& FieldName, ENPCBehaviorMode& OutMode)
    {
        return TryParseEnumFromJson(JsonObj, FieldName, OutMode);
    }

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



    bool TryExtractGameAction(TSharedPtr<FJsonObject> ActionObj, FGameAction& OutAction)
    {
        if (!ActionObj.IsValid()) return false;

        TryParseEnumFromJson(ActionObj, TEXT("ActionType"), OutAction.ActionType);

        TryParseEnumFromJson(ActionObj, TEXT("FacialState"), OutAction.FacialState);

        ExtractActionParameters(ActionObj, OutAction.Parameters);
        return true;
    }

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

FString UMCPJsonUtils::SerializePerceptionReport(const FString& AgentID, const TArray<FPerceptionData>& PerceptionEvents)
{
    TArray<TSharedPtr<FJsonValue>> EventValues;
    EventValues.Reserve(PerceptionEvents.Num());

    for (const FPerceptionData& Event : PerceptionEvents)
    {
        TSharedPtr<FJsonObject> EventObj = MakeShared<FJsonObject>();
        EventObj->SetStringField(
            TEXT("target_id"),
            Event.TargetID.IsEmpty() ? TEXT("Unknown") : Event.TargetID);
        EventObj->SetStringField(TEXT("sense_type"), SenseTypeToString(Event.SenseType));
        EventObj->SetObjectField(TEXT("location"), ConvertLocationToJson(Event.Location));
        EventObj->SetNumberField(TEXT("distance"), Event.Distance);
        EventObj->SetNumberField(TEXT("danger_score"), FMath::Clamp(Event.DangerScore, 0.f, 1.f));

        EventValues.Add(MakeShared<FJsonValueObject>(EventObj));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("agent_id"), AgentID);
    Root->SetArrayField(TEXT("perceptions"), EventValues);
    Root->SetNumberField(TEXT("generated_at"),
        (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds());

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    return Output;
}

bool UMCPJsonUtils::ParseLocationDecisionResult(
    const FString& Json, FString& OutAgentId, FString& OutChosenId)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

    // 타입 확인
    FString TypeStr;
    if (!Root->TryGetStringField(TEXT("type"), TypeStr) || TypeStr != TEXT("location_decision_result"))
        return false;

    // payload에서 agent_id, chosen_id 추출
    const TSharedPtr<FJsonObject>* PayloadObj;
    if (!Root->TryGetObjectField(TEXT("payload"), PayloadObj)) return false;

    return (*PayloadObj)->TryGetStringField(TEXT("agent_id"), OutAgentId)
        && (*PayloadObj)->TryGetStringField(TEXT("chosen_id"), OutChosenId);
}
