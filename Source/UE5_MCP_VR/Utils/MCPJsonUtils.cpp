#include "MCPJsonUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

bool UMCPJsonUtils::ParseActionBatch(FString Json, FActionBatch& OutBatch)
{
    // 1. Create Reader
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

    // 2. Deserialize
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return false;
    }

    // 3. Extract Agent ID
    OutBatch.AgentID = RootObject->GetStringField(TEXT("agent_id"));

    // 4. Extract Actions
    const TArray<TSharedPtr<FJsonValue>>* ActionsArray;
    if (RootObject->TryGetArrayField(TEXT("actions"), ActionsArray))
    {
        for (const TSharedPtr<FJsonValue>& Val : *ActionsArray)
        {
            TSharedPtr<FJsonObject> ActionObj = Val->AsObject();
            if (!ActionObj.IsValid()) continue;

            FGameAction NewAction;
            // Parse common fields
            NewAction.ActionType = ActionObj->GetStringField(TEXT("action_type"));
            
            // Optional TargetID
            if (ActionObj->HasField(TEXT("target_id")))
            {
                NewAction.TargetID = ActionObj->GetStringField(TEXT("target_id"));
            }
            else if (ActionObj->HasField(TEXT("target_listener")))
            {
                NewAction.TargetID = ActionObj->GetStringField(TEXT("target_listener"));
            }

            // Parse Parameters Map (Mixed Types -> String)
            const TSharedPtr<FJsonObject>* ParamsObj;
            if (ActionObj->TryGetObjectField(TEXT("parameters"), ParamsObj))
            {
                for (const auto& Pair : (*ParamsObj)->Values)
                {
                    FString Key = Pair.Key;
                    TSharedPtr<FJsonValue> Value = Pair.Value;
                    FString StringValue;

                    switch (Value->Type)
                    {
                    case EJson::String:
                        StringValue = Value->AsString();
                        break;
                    case EJson::Number:
                        // Convert number to string
                        StringValue = FString::SanitizeFloat(Value->AsNumber());
                        break;
                    case EJson::Boolean:
                        StringValue = Value->AsBool() ? TEXT("true") : TEXT("false");
                        break;
                    default:
                        StringValue = TEXT("UnknownType");
                        break;
                    }
                    NewAction.Parameters.Add(Key, StringValue);
                }
            }

            OutBatch.Actions.Add(NewAction);
        }
    }

    return true;
}
