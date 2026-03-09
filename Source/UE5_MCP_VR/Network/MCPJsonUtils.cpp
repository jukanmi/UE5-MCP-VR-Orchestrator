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

}




namespace
{
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
