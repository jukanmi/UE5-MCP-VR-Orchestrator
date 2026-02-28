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
    // TODO: [UE5] Python 서버가 새로운 MessageEnvelope 구조를 요구하므로 (type, auth_token, msg_id 등 필요), 
    // 여기서는 PayloadObject만 조립하는 JSON 문자열을 생성하여 반환하고, 
    // 이를 호출하는 곳(NPCManager::SendStateToMCP)에서 FEnvelopeBuilder::BuildStateUpdate()로 감싸도록 구조를 리팩토링하세요.
    // 기존의 RootObject 메타데이터 설정 코드는 삭제될 수 있습니다.

    // 루트 JSON 봉투(Envelope) 객체 생성
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

    // --- Envelope 메타데이터 ---
    // 메시지 순서 추적 및 실패 콜백 매칭을 위해 GUID 기반 고유 msg_id를 자동 생성합니다.
    RootObject->SetStringField(TEXT("msg_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    RootObject->SetStringField(TEXT("type"), TEXT("state_update"));
    RootObject->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());

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

    // 근처 개체 배열 (필터링된 최소 컨텍스트만 포함)
    TArray<TSharedPtr<FJsonValue>> EntitiesArray;
    for (const FEntityState& Entity : StateData.NearbyEntities)
    {
        TSharedPtr<FJsonObject> EntityObject = MakeShared<FJsonObject>();
        EntityObject->SetStringField(TEXT("id"), Entity.EntityID);
        EntityObject->SetNumberField(TEXT("distance"), FMath::RoundToFloat(Entity.Distance));
        EntityObject->SetBoolField(TEXT("is_hostile"), Entity.bIsHostile);
        EntitiesArray.Add(MakeShared<FJsonValueObject>(EntityObject));
    }
    PayloadObject->SetArrayField(TEXT("nearby_entities"), EntitiesArray);

    // EQS 결과 배열 (최대 3개 좌표만 포함)
    TArray<TSharedPtr<FJsonValue>> EQSArray;
    int32 EQSCount = FMath::Min(StateData.EQSResults.Num(), 3); // 데이터 다이어트: 최대 3개
    for (int32 i = 0; i < EQSCount; i++)
    {
        const FEQSResult& EQS = StateData.EQSResults[i];
        TSharedPtr<FJsonObject> EQSObject = MakeShared<FJsonObject>();
        EQSObject->SetStringField(TEXT("tag"), EQS.QueryTag);
        EQSObject->SetNumberField(TEXT("score"), EQS.Score);

        TSharedPtr<FJsonObject> EQSLocation = MakeShared<FJsonObject>();
        EQSLocation->SetNumberField(TEXT("x"), FMath::RoundToFloat(EQS.BestLocation.X * 100.f) / 100.f);
        EQSLocation->SetNumberField(TEXT("y"), FMath::RoundToFloat(EQS.BestLocation.Y * 100.f) / 100.f);
        EQSLocation->SetNumberField(TEXT("z"), FMath::RoundToFloat(EQS.BestLocation.Z * 100.f) / 100.f);
        EQSObject->SetObjectField(TEXT("location"), EQSLocation);

        EQSArray.Add(MakeShared<FJsonValueObject>(EQSObject));
    }
    PayloadObject->SetArrayField(TEXT("eqs_results"), EQSArray);

    // 페이로드를 루트 봉투에 결합합니다.
    RootObject->SetObjectField(TEXT("payload"), PayloadObject);

    // 최종 직렬화
    FString OutputJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
    if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
    {
        UE_LOG(LogTemp, Error, TEXT("[MCPJsonUtils] Failed to serialize GameStateData to JSON."));
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
