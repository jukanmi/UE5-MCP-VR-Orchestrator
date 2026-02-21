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
            // This ensures top-level fields like 'text' in Dialogue action are captured
            for (const auto& Pair : ActionObj->Values)
            {
                FString Key = Pair.Key;
                
                // Skip metadata fields already handled or handled separately
                if (Key == TEXT("action_type")) continue;
                
                if (Key == TEXT("behavior_mode"))
                {
                    NewAction.BehaviorMode = Pair.Value->AsString();
                    continue;
                }
                if (Key == TEXT("facial_state"))
                {
                    NewAction.FacialState = Pair.Value->AsString();
                    continue;
                }

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
                        // 중첩 오브젝트(target_loc 등)는 JSON 문자열로 직렬화하여 저장
                        // 왜: FGameAction::Parameters는 TMap<FString, FString>이므로
                        // 중첩 오브젝트를 문자열로 변환해야 함. ProcessNextAction에서 다시 파싱.
                        if (SubPair.Value->Type == EJson::Object)
                        {
                            TSharedRef<FJsonObject> NestedObj = SubPair.Value->AsObject().ToSharedRef();
                            FString NestedStr;
                            TSharedRef<TJsonWriter<>> NestedWriter = TJsonWriterFactory<>::Create(&NestedStr);
                            FJsonSerializer::Serialize(NestedObj, NestedWriter);
                            NewAction.Parameters.Add(SubPair.Key, NestedStr);
                        }
                        else
                        {
                            NewAction.Parameters.Add(SubPair.Key, SubPair.Value->AsString());
                        }
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
/**
 * @brief Parses a JSON string containing an array of action batches.
 * @param Json The JSON string to parse.
 * @param OutBatches The array to store the parsed action batches.
 * @return true if the JSON was parsed successfully, false otherwise.
 */
bool UMCPJsonUtils::ParseActionBatchArray(FString Json, TArray<FActionBatch>& OutBatches)
{
    // 1. Create Reader
    TArray<TSharedPtr<FJsonValue>> RootArray;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

    // 2. Deserialize
    if (!FJsonSerializer::Deserialize(Reader, RootArray))
    {
        return false;
    }

    // 3. Loop through array
    for (const TSharedPtr<FJsonValue>& Val : RootArray)
    {
        TSharedPtr<FJsonObject> BatchObj = Val->AsObject();
        if (!BatchObj.IsValid()) continue;

        FActionBatch NewBatch;
        
        // Extract Agent ID
        NewBatch.AgentID = BatchObj->GetStringField(TEXT("agent_id"));

        // Extract Actions
        const TArray<TSharedPtr<FJsonValue>>* ActionsArray;
        if (BatchObj->TryGetArrayField(TEXT("actions"), ActionsArray))
        {
            for (const TSharedPtr<FJsonValue>& ActionVal : *ActionsArray)
            {
                TSharedPtr<FJsonObject> ActionObj = ActionVal->AsObject();
                if (!ActionObj.IsValid()) continue;

                FGameAction NewAction;
                // 1. Parse common fields
                NewAction.ActionType = ActionObj->GetStringField(TEXT("action_type"));
                
                // 2. Collect ALL fields into Parameters
                for (const auto& Pair : ActionObj->Values)
                {
                    FString Key = Pair.Key;
                    
                    if (Key == TEXT("action_type")) continue;

                    if (Key == TEXT("behavior_mode"))
                    {
                        NewAction.BehaviorMode = Pair.Value->AsString();
                        continue;
                    }
                    if (Key == TEXT("facial_state"))
                    {
                        NewAction.FacialState = Pair.Value->AsString();
                        continue;
                    }

                    // Handle Target IDs
                    if (Key == TEXT("target_id") || Key == TEXT("target_listener") || Key == TEXT("executor_npc_id"))
                    {
                        if (Key == TEXT("target_id"))
                        {
                            NewAction.TargetID = Pair.Value->AsString();
                        }
                        NewAction.Parameters.Add(Key, Pair.Value->AsString());
                        continue;
                    }

                    // Handle nested 'parameters' object
                    if (Key == TEXT("parameters") && Pair.Value->Type == EJson::Object)
                    {
                        TSharedPtr<FJsonObject> SubParams = Pair.Value->AsObject();
                        for (const auto& SubPair : SubParams->Values)
                        {
                            // 중첩 오브젝트(target_loc 등)는 JSON 문자열로 직렬화
                            if (SubPair.Value->Type == EJson::Object)
                            {
                                TSharedRef<FJsonObject> NestedObj = SubPair.Value->AsObject().ToSharedRef();
                                FString NestedStr;
                                TSharedRef<TJsonWriter<>> NestedWriter = TJsonWriterFactory<>::Create(&NestedStr);
                                FJsonSerializer::Serialize(NestedObj, NestedWriter);
                                NewAction.Parameters.Add(SubPair.Key, NestedStr);
                            }
                            else
                            {
                                NewAction.Parameters.Add(SubPair.Key, SubPair.Value->AsString());
                            }
                        }
                        continue;
                    }

                    // Add everything else as string
                    NewAction.Parameters.Add(Key, Pair.Value->AsString());
                }

                NewBatch.Actions.Add(NewAction);
            }
        }
        
        OutBatches.Add(NewBatch);
    }

    return true;
}
