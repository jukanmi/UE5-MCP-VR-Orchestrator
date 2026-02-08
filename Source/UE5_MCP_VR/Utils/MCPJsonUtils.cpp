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
            // 1. Parse common fields
            NewAction.ActionType = ActionObj->GetStringField(TEXT("action_type"));
            
            // 2. Collect ALL fields into Parameters for flexibility
            // This ensures top-level fields like 'text' in SpeakAction are captured
            for (const auto& Pair : ActionObj->Values)
            {
                FString Key = Pair.Key;
                
                // Skip metadata fields already handled or handled separately
                if (Key == TEXT("action_type")) continue;

                // Handle Target IDs
                if (Key == TEXT("target_id") || Key == TEXT("target_listener"))
                {
                    NewAction.TargetID = Pair.Value->AsString();
                    // Still add to parameters for backward compatibility in logic
                    NewAction.Parameters.Add(Key, NewAction.TargetID);
                    continue;
                }

                // Handle nested 'parameters' object if it exists (legacy support)
                if (Key == TEXT("parameters") && Pair.Value->Type == EJson::Object)
                {
                    TSharedPtr<FJsonObject> SubParams = Pair.Value->AsObject();
                    for (const auto& SubPair : SubParams->Values)
                    {
                        NewAction.Parameters.Add(SubPair.Key, SubPair.Value->AsString());
                    }
                    continue;
                }

                // Add everything else as a string
                NewAction.Parameters.Add(Key, Pair.Value->AsString());
            }

            OutBatch.Actions.Add(NewAction);
        }
    }

    return true;
}
