#include "MCPJsonUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"

/**
 * FGameStateData를 Python 백엔드가 읽을 수 있는 state_update JSON으로 직렬화합니다.
 * auth_token은 호출 측에서 별도 보안 채널로 주입받아야 하지만,
 * 여기서는 구조적 완성도를 위한 플레이스홀더로 빈 문자열을 사용합니다.
 */
FString UMCPJsonUtils::SerializeGameState(const FGameStateData& StateData)
{
    // --- 상태 페이로드 (Payload) ---
    TSharedPtr<FJsonObject> PayloadObject = MakeShared<FJsonObject>();
    PayloadObject->SetStringField(TEXT("owner_agent_id"), StateData.OwnerAgentID);
    PayloadObject->SetStringField(TEXT("threat_level"), StateData.ThreatLevel);
    PayloadObject->SetBoolField(TEXT("in_cover"), StateData.bIsInCover);
    PayloadObject->SetBoolField(TEXT("line_of_sight"), StateData.bHasLineOfSight);

    // 소유자(NPC) 위치 - 소수점 2자리로 제한해 토큰 낭비를 막습니다.
    TSharedPtr<FJsonObject> LocationObject = MakeShared<FJsonObject>();
    LocationObject->SetNumberField(TEXT("x"), FMath::RoundToFloat(StateData.OwnerLocation.X * 100.f) / 100.f);
    LocationObject->SetNumberField(TEXT("y"), FMath::RoundToFloat(StateData.OwnerLocation.Y * 100.f) / 100.f);
    LocationObject->SetNumberField(TEXT("z"), FMath::RoundToFloat(StateData.OwnerLocation.Z * 100.f) / 100.f);
    PayloadObject->SetObjectField(TEXT("owner_location"), LocationObject);

    // 순수 시각/청각 인지 결과(Perception) JSON 파싱 로직
    TArray<TSharedPtr<FJsonValue>> PerceptionArray;
    for (const FPerceptionData& Target : StateData.PerceivedTargets)
    {
        TSharedPtr<FJsonObject> TargetObj = MakeShared<FJsonObject>();
        // 고유 ID 또는 정체 불명 시 "unknown"
        TargetObj->SetStringField(TEXT("target_id"), Target.TargetID);
        
        // [의도] Enum 구조체의 타입 안정성을 챙기되 JSON 전송 규격에 맞게 파싱하여 전달합니다.
        FString SenseStr = TEXT("Other");
        if (Target.SenseType == ESenseType::Sight) SenseStr = TEXT("Sight");
        else if (Target.SenseType == ESenseType::Hearing) SenseStr = TEXT("Hearing");

        TargetObj->SetStringField(TEXT("sense_type"), SenseStr);
        TargetObj->SetNumberField(TEXT("distance"), FMath::RoundToFloat(Target.Distance));
        
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetNumberField(TEXT("x"), FMath::RoundToFloat(Target.Location.X * 100.f) / 100.f);
        LocObj->SetNumberField(TEXT("y"), FMath::RoundToFloat(Target.Location.Y * 100.f) / 100.f);
        LocObj->SetNumberField(TEXT("z"), FMath::RoundToFloat(Target.Location.Z * 100.f) / 100.f);
        TargetObj->SetObjectField(TEXT("location"), LocObj);
        
        PerceptionArray.Add(MakeShared<FJsonValueObject>(TargetObj));
    }
    PayloadObject->SetArrayField(TEXT("perceived_targets"), PerceptionArray);

    // 최종 직렬화: Envelope 포장 없이 순수 Payload Object만 직렬화합니다.
    FString OutputJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
    if (!FJsonSerializer::Serialize(PayloadObject.ToSharedRef(), Writer))
    {
        UE_LOG(LogTemp, Error, TEXT("[MCPJsonUtils] Failed to serialize GameStateData Payload to JSON."));
        return FString();
    }
    return OutputJson;
}




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
