#include "MCPJsonUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"




bool UMCPJsonUtils::ParseModeActionRequest(FString Json, FModeActionRequest& OutRequest)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return false;
    }

    // Check for "ActionBatches" field (Sign of FModeActionRequest)
    if (!RootObject->HasField(TEXT("ActionBatches"))) return false;

    // 1. Mode
    FString ModeStr = RootObject->GetStringField(TEXT("Mode"));
    const UEnum* ModeEnum = StaticEnum<ENPCBehaviorMode>();
    if (ModeEnum)
    {
        int64 EnumVal = ModeEnum->GetValueByNameString(ModeStr);
        if (EnumVal != INDEX_NONE) OutRequest.Mode = (ENPCBehaviorMode)EnumVal;
    }

    // 2. ActionBatches (Map)
    TSharedPtr<FJsonObject> BatchesObj = RootObject->GetObjectField(TEXT("ActionBatches"));
    if (BatchesObj.IsValid())
    {
        for (const auto& Pair : BatchesObj->Values)
        {
            FString AgentID = Pair.Key;
            TSharedPtr<FJsonObject> BatchObj = Pair.Value->AsObject();
            if (!BatchObj.IsValid()) continue;

            FActionBatch NewBatch;
            NewBatch.AgentID = AgentID;
            
            // Mode in Batch
            FString BatchModeStr = BatchObj->GetStringField(TEXT("Mode"));
            if (ModeEnum)
            {
                int64 EnumVal = ModeEnum->GetValueByNameString(BatchModeStr);
                if (EnumVal != INDEX_NONE) NewBatch.Mode = (ENPCBehaviorMode)EnumVal;
            }

            // Actions in Batch
            const TArray<TSharedPtr<FJsonValue>>* ActionsArray;
            if (BatchObj->TryGetArrayField(TEXT("Actions"), ActionsArray))
            {
                for (const TSharedPtr<FJsonValue>& Val : *ActionsArray)
                {
                    TSharedPtr<FJsonObject> ActionObj = Val->AsObject();
                    if (!ActionObj.IsValid()) continue;

                    FGameAction NewAction;
                    
                    // ActionType (Accept ActionType or action_type)
                    FString ActionTypeStr;
                    if (ActionObj->TryGetStringField(TEXT("ActionType"), ActionTypeStr) || 
                        ActionObj->TryGetStringField(TEXT("action_type"), ActionTypeStr))
                    {
                        const UEnum* ActionEnum = StaticEnum<EAction>();
                        if (ActionEnum)
                        {
                            int64 EnumVal = ActionEnum->GetValueByNameString(ActionTypeStr);
                            if (EnumVal != INDEX_NONE) NewAction.ActionType = (EAction)EnumVal;
                        }
                    }

                    // FacialState (Accept FacialState or facial_state) - Optional
                    FString FacialStr;
                    if (ActionObj->TryGetStringField(TEXT("FacialState"), FacialStr) || 
                        ActionObj->TryGetStringField(TEXT("facial_state"), FacialStr))
                    {
                        const UEnum* FacialEnum = StaticEnum<EFacialState>();
                        if (FacialEnum)
                        {
                            int64 EnumVal = FacialEnum->GetValueByNameString(FacialStr);
                            if (EnumVal != INDEX_NONE) NewAction.FacialState = (EFacialState)EnumVal;
                        }
                    }

                    // Parameters - Optional
                    const TSharedPtr<FJsonObject>* ParamsObj;
                    if (ActionObj->TryGetObjectField(TEXT("Parameters"), ParamsObj) || 
                        ActionObj->TryGetObjectField(TEXT("parameters"), ParamsObj))
                    {
                        for (const auto& ParamPair : (*ParamsObj)->Values)
                        {
                            if (ParamPair.Value->Type == EJson::Object)
                            {
                                FString NestedStr;
                                TSharedRef<TJsonWriter<>> NestedWriter = TJsonWriterFactory<>::Create(&NestedStr);
                                FJsonSerializer::Serialize(ParamPair.Value->AsObject().ToSharedRef(), NestedWriter);
                                NewAction.Parameters.Add(ParamPair.Key, NestedStr);
                            }
                            else
                            {
                                NewAction.Parameters.Add(ParamPair.Key, ParamPair.Value->AsString());
                            }
                        }
                    }
                    NewBatch.Actions.Add(NewAction);
                }
            }
            OutRequest.ActionBatches.Add(AgentID, NewBatch);
        }
    }

    return true;
}
