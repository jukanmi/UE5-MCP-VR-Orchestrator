#include "MCPJsonUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "NPC/Struct/NPCActionKeys.h"

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
        if (JsonObj->TryGetStringField(FieldName, EnumString))
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
        if (ActionObj->TryGetObjectField(NPCActionKeys::Proto_Parameters, ParamsObj))
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

        TryParseEnumFromJson(ActionObj, NPCActionKeys::Proto_ActionType, OutAction.ActionType);

        TryParseEnumFromJson(ActionObj, NPCActionKeys::Proto_FacialState, OutAction.FacialState);

        ExtractActionParameters(ActionObj, OutAction.Parameters);
        return true;
    }

    bool TryExtractActionBatch(const FString& AgentID, TSharedPtr<FJsonObject> BatchObj, FActionBatch& OutBatch)
    {
        if (!BatchObj.IsValid()) return false;

        OutBatch.AgentID = AgentID;
        
        TryParseBehaviorMode(BatchObj, NPCActionKeys::Proto_Mode, OutBatch.Mode);

        const TArray<TSharedPtr<FJsonValue>>* ActionsArray;
        if (BatchObj->TryGetArrayField(NPCActionKeys::Proto_Actions, ActionsArray))
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

bool UMCPJsonUtils::ParseModeActionRequestFromObject(const TSharedPtr<FJsonObject>& Root, FModeActionRequest& OutRequest)
{
    if (!Root.IsValid() || !Root->HasField(NPCActionKeys::Proto_ActionBatches)) return false;

    TryParseBehaviorMode(Root, NPCActionKeys::Proto_Mode, OutRequest.Mode);

    // TryGet 패턴 — ActionBatches 가 object 가 아닌 비정상 페이로드에서도 무음 통과(에러 로그 노이즈 방지)
    const TSharedPtr<FJsonObject>* BatchesObjPtr = nullptr;
    if (Root->TryGetObjectField(NPCActionKeys::Proto_ActionBatches, BatchesObjPtr) && BatchesObjPtr)
    {
        for (const auto& Pair : (*BatchesObjPtr)->Values)
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

bool UMCPJsonUtils::ParseModeActionRequest(FString Json, FModeActionRequest& OutRequest)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return false;
    }
    return ParseModeActionRequestFromObject(RootObject, OutRequest);
}

FString UMCPJsonUtils::SerializePerceptionReport(const FString& AgentID, const TArray<FPerceptionData>& PerceptionEvents, const FString& ReportType, const FString& ReflexAction)
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
    // 특수 보고(승리 등)만 명시 — 일반 perception 은 필드 생략(Python 기본값 "perception" 폴백).
    if (!ReportType.IsEmpty())
    {
        Root->SetStringField(TEXT("report_type"), ReportType);
    }
    // 척수반사 실행 이력 — 미발동이면 필드 생략(Python 기본값 "" 폴백, 하위호환).
    if (!ReflexAction.IsEmpty())
    {
        Root->SetStringField(TEXT("reflex_action"), ReflexAction);
    }

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    return Output;
}

bool UMCPJsonUtils::ParseLocationDecisionResultFromObject(
    const TSharedPtr<FJsonObject>& Root, FString& OutAgentId, FString& OutChosenId, FString& OutReason, int32& OutRequestGen)
{
    OutRequestGen = 0;
    if (!Root.IsValid()) return false;

    FString TypeStr;
    if (!Root->TryGetStringField(TEXT("type"), TypeStr) || TypeStr != TEXT("location_decision_result"))
        return false;

    const TSharedPtr<FJsonObject>* PayloadObj;
    if (!Root->TryGetObjectField(TEXT("payload"), PayloadObj)) return false;

    (*PayloadObj)->TryGetStringField(TEXT("reason"), OutReason);     // optional
    (*PayloadObj)->TryGetNumberField(TEXT("request_gen"), OutRequestGen); // optional — 미포함 시 0 유지(stale 검사 우회)
    return (*PayloadObj)->TryGetStringField(TEXT("agent_id"), OutAgentId)
        && (*PayloadObj)->TryGetStringField(TEXT("chosen_id"), OutChosenId);
}

bool UMCPJsonUtils::ParseLocationDecisionResult(
    const FString& Json, FString& OutAgentId, FString& OutChosenId, FString& OutReason, int32& OutRequestGen)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
    return ParseLocationDecisionResultFromObject(Root, OutAgentId, OutChosenId, OutReason, OutRequestGen);
}

bool UMCPJsonUtils::ParseAffinityUpdateFromObject(
    const TSharedPtr<FJsonObject>& Root, FString& OutAgentId, TMap<FString, int32>& OutRelations)
{
    if (!Root.IsValid()) return false;

    FString Status;
    if (!Root->TryGetStringField(TEXT("status"), Status) || Status != TEXT("cached")) return false;
    if (!Root->TryGetStringField(TEXT("agent_id"), OutAgentId)) return false;

    const TArray<TSharedPtr<FJsonValue>>* RelationsArray;
    if (!Root->TryGetArrayField(TEXT("relations"), RelationsArray)) return false;

    for (const TSharedPtr<FJsonValue>& Entry : *RelationsArray)
    {
        const TSharedPtr<FJsonObject>* EntryObj;
        if (!Entry->TryGetObject(EntryObj)) continue;

        FString TargetID;
        int32 Score = 0;
        if (!(*EntryObj)->TryGetStringField(TEXT("target_id"), TargetID)) continue;
        (*EntryObj)->TryGetNumberField(TEXT("affinity_score"), Score);

        OutRelations.Add(TargetID, Score);
    }

    return true;
}

bool UMCPJsonUtils::ParseAffinityUpdate(const FString& Json, FString& OutAgentId, TMap<FString, int32>& OutRelations)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
    return ParseAffinityUpdateFromObject(Root, OutAgentId, OutRelations);
}

bool UMCPJsonUtils::ParseNpcAudioResponseFromObject(
    const TSharedPtr<FJsonObject>& Root,
    FString& OutNpcId,
    FString& OutWsUrl,
    int32& OutSampleRate,
    int32& OutChannels,
    FString& OutDialogueText,
    FString& OutEmotion)
{
    if (!Root.IsValid()) return false;

    FString TypeStr;
    if (!Root->TryGetStringField(NPCActionKeys::Audio_Type, TypeStr)) return false;
    if (TypeStr != NPCActionKeys::Audio_TypeValue) return false;

    Root->TryGetStringField(NPCActionKeys::Audio_NpcId, OutNpcId);
    Root->TryGetStringField(NPCActionKeys::Audio_DialogueText, OutDialogueText);

    OutSampleRate = 16000;
    OutChannels = 1;
    OutEmotion = TEXT("neutral");
    OutWsUrl.Reset();

    const TSharedPtr<FJsonObject>* StreamObj = nullptr;
    if (Root->TryGetObjectField(NPCActionKeys::Audio_Stream, StreamObj) && StreamObj && StreamObj->IsValid())
    {
        (*StreamObj)->TryGetStringField(NPCActionKeys::Audio_StreamUrl, OutWsUrl);
        (*StreamObj)->TryGetNumberField(NPCActionKeys::Audio_SampleRate, OutSampleRate);
        (*StreamObj)->TryGetNumberField(NPCActionKeys::Audio_Channels, OutChannels);
    }

    const TSharedPtr<FJsonObject>* AnimObj = nullptr;
    if (Root->TryGetObjectField(NPCActionKeys::Audio_AnimMetadata, AnimObj) && AnimObj && AnimObj->IsValid())
    {
        (*AnimObj)->TryGetStringField(NPCActionKeys::Audio_Emotion, OutEmotion);
    }

    return !OutNpcId.IsEmpty();
}
