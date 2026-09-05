#include "NPC/Subsystems/NPCManager.h"
#include "NPC/BP/SmartNPC.h"
#include "NPC/Components/NPCStateComponent.h"
#include "NPC/Components/NPCInventoryComponent.h"
#include "NPC/Components/NPCAudioStreamComponent.h"
#include "Network/MCPJsonUtils.h"
#include "Network/EnvelopeBuilder.h"
#include "NPC/Action/NPCActionComponent.h"
#include "Furniture/Subsystems/FurnitureManager.h"
#include "Furniture/BP/FurnitureActor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"


UNPCManager* UNPCManager::Get(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    return GI ? GI->GetSubsystem<UNPCManager>() : nullptr;
}


// --- UNPCMap ---


void UNPCMap::DeliverToNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch)
{
    if (ASmartNPC* TargetNPC = GetValidNPC(TargetAgentID))
    {
        TargetNPC->ExecuteActionBatch(ActionBatch);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCMap] Skipping Dispatch! Target NPC '%s' is not valid."), *TargetAgentID);
    }
}


void UNPCMap::DeliverLocationDecision(const FString& AgentID, const FString& ChosenCandidateId, const FString& Reason, uint32 RequestGen)
{
    ASmartNPC* NPC = GetValidNPC(AgentID);
    if (!NPC)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCMap] DeliverLocationDecision - NPC '%s' 없음"), *AgentID);
        return;
    }
    if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
    {
        ActionComp->NotifyLocationDecisionReady(ChosenCandidateId, Reason, RequestGen);
    }
}

void UNPCMap::DeliverParsedActionBatches(const TSharedPtr<FJsonObject>& Root)
{
    if (!Root.IsValid() || !Root->HasField(TEXT("ActionBatches")))
    {
        FString StatusValue;
        if (Root.IsValid()) Root->TryGetStringField(TEXT("status"), StatusValue);
        UE_LOG(LogTemp, Verbose,
            TEXT("[NPCMap] Non-action response received (status='%s'). Skipping dispatch."),
            *StatusValue);
        return;
    }

    FModeActionRequest ParsedRequest;
    if (!UMCPJsonUtils::ParseModeActionRequestFromObject(Root, ParsedRequest))
    {
        UE_LOG(LogTemp, Error, TEXT("[NPCMap] Failed to Parse ModeActionRequest from JSON object."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Request Validated! Master Mode: %d, BatchCount: %d"),
        static_cast<int32>(ParsedRequest.Mode), ParsedRequest.ActionBatches.Num());

    for (const auto& BatchPair : ParsedRequest.ActionBatches)
    {
        DeliverToNPC(BatchPair.Key, BatchPair.Value);
    }
}

void UNPCMap::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    UE_LOG(LogTemp, Log, TEXT("[NPCMap] Received WebSocket Payload (Size: %d bytes)"), JsonMessage.Len());

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonMessage);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[NPCMap] Received non-JSON or malformed message. Raw (first 200 chars): %.200s"),
            *JsonMessage);
        return;
    }

    DeliverParsedActionBatches(Root);
}

// --- UNPCManager ---

void UNPCManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    NPCMap = NewObject<UNPCMap>(this);

    LLMClient = NewObject<ULLMNetworkClient>(this);
    if (LLMClient)
    {
        LLMClient->OnMessageReceived.AddDynamic(this, &UNPCManager::OnLLMMessageReceived);
        LLMClient->InitializeLLM();
    }

    // 주기적 state_update 시작 — Python으로부터 affinity 변화를 받아 AffinityCache 갱신
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            StateUpdateTimerHandle,
            FTimerDelegate::CreateUObject(this, &UNPCManager::TickStateUpdate),
            StateUpdateInterval, true, StateUpdateInterval);
    }
}

void UNPCManager::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StateUpdateTimerHandle);
    }

    if (LLMClient) LLMClient->Disconnect();

    NPCMap = nullptr;
    LLMClient = nullptr;

    Super::Deinitialize();
}

void UNPCManager::TickStateUpdate()
{
    if (!NPCMap || !LLMClient || !LLMClient->IsConnected()) return;

    // 이전 응답이 아직 안 온 NPC는 재전송 건너뜀 — 응답 적체로 인한 연속 갱신 방지
    for (const TPair<FString, ASmartNPC*>& Pair : NPCMap->GetActiveNPCs())
    {
        ASmartNPC* NPC = Pair.Value;
        if (!IsValid(NPC)) continue;

        const FString& AgentID = Pair.Key;
        if (PendingStateUpdateAgents.Contains(AgentID))
        {
            UE_LOG(LogTemp, Verbose, TEXT("[NPCManager] %s state_update 스킵 — 응답 대기 중"), *AgentID);
            continue;
        }

        FGameStateData StateData;
        StateData.OwnerAgentID = AgentID;
        StateData.OwnerLocation = NPC->GetActorLocation();
        // BehaviorMode 단일 소유 = NPCStateComponent. 하드코딩 대신 실제 상태 반영.
        StateData.CurrentMode = NPC->GetStateComponent()
            ? NPC->GetStateComponent()->GetBehaviorMode()
            : ENPCBehaviorMode::Common;

        PendingStateUpdateAgents.Add(AgentID);
        LLMClient->SendStateUpdate(StateData);
    }
}

void UNPCManager::RegisterNPC(const FString& AgentID, ASmartNPC* NPC)
{
    if (NPCMap && NPC)
    {
        NPCMap->RegisterNPC(AgentID, NPC);

        if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
        {
            ActionComp->OnNPCDialogue.AddDynamic(this, &UNPCManager::HandleNPCDialogue);
        }
    }
}

void UNPCManager::UnregisterNPC(const FString& AgentID)
{
    if (NPCMap)
    {
        if (ASmartNPC* NPC = NPCMap->GetValidNPC(AgentID))
        {
            if (UNPCActionComponent* ActionComp = NPC->GetActionComponent())
            {
                ActionComp->OnNPCDialogue.RemoveDynamic(this, &UNPCManager::HandleNPCDialogue);
            }
        }
        NPCMap->UnregisterNPC(AgentID);
    }
}

ASmartNPC* UNPCManager::GetNPCById(const FString& AgentID) const
{
    return NPCMap ? NPCMap->GetValidNPC(AgentID) : nullptr;
}

void UNPCManager::OnWebSocketMessageReceived(const FString& JsonMessage)
{
    if (NPCMap)
    {
        NPCMap->OnWebSocketMessageReceived(JsonMessage);
    }
}


bool UNPCManager::IsServerConnected() const
{
    return LLMClient ? LLMClient->IsConnected() : false;
}

void UNPCManager::SendEnvelopePromptToLLM(const FString& JsonData)
{
    if (LLMClient)
    {
        LLMClient->SendPrompt(JsonData);
    }
}

void UNPCManager::SendPlayerDialogue(const FString& PlayerID, const FString& TargetNpcId, const FString& Text)
{
    if (Text.IsEmpty() || TargetNpcId.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] SendPlayerDialogue 스킵 — Text/TargetNpcId 비어있음 (target=%s)"), *TargetNpcId);
        return;
    }
    if (!LLMClient || !LLMClient->IsConnected())
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] SendPlayerDialogue 스킵 — LLM 서버 미연결"));
        return;
    }

    // PromptPayload 조립 — payload 키는 snake_case
    const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("player_id"), PlayerID);
    Payload->SetStringField(TEXT("voice_transcript"), Text);
    Payload->SetStringField(TEXT("target_npc_id"), TargetNpcId);

    // ── 계획 캐싱 분기 (Multi-NPC Cached Planning) ────────────────────────
    // 대상 NPC 의 ShouldReplan 판정 → requires_replan 송신.
    // 턴 상한/plan 유무/danger pending 플래그로 판정 (danger 는 perception 경로의 FlagDangerReplan 경유).
    // 재계획 불필요 시 보관 plan 을 current_plan(snake_case)으로 동봉 → e4b 단독 컨텍스트 주입.
    bool bRequiresReplan = true;
    if (ASmartNPC* TargetNPC = GetNPCById(TargetNpcId))
    {
        if (UNPCStateComponent* StateComp = TargetNPC->GetStateComponent())
        {
            bRequiresReplan = StateComp->ShouldReplan();
            if (!bRequiresReplan)
            {
                // 이 plan 으로 한 턴 더 간다 — 수명 카운터를 올린다. 재계획하는 턴에는
                // 세지 않는다(Stage2 가 새 plan 을 주면 SetCurrentPlan 이 0 으로 리셋).
                StateComp->NotePlanTurnElapsed();

                const FNPCPlan& Plan = StateComp->GetCurrentPlan();
                const TSharedRef<FJsonObject> PlanJson = MakeShared<FJsonObject>();
                PlanJson->SetStringField(TEXT("goal"), Plan.Goal);
                TArray<TSharedPtr<FJsonValue>> StepsArr;
                for (const FString& Step : Plan.Steps)
                {
                    StepsArr.Add(MakeShared<FJsonValueString>(Step));
                }
                PlanJson->SetArrayField(TEXT("steps"), StepsArr);
                PlanJson->SetNumberField(TEXT("relation_snapshot"), Plan.RelationSnapshot);

                // current_plan: npc_id → plan (Python current_plan 구조와 정합).
                const TSharedRef<FJsonObject> CurrentPlanJson = MakeShared<FJsonObject>();
                CurrentPlanJson->SetObjectField(TargetNpcId, PlanJson);
                Payload->SetObjectField(TEXT("current_plan"), CurrentPlanJson);
            }
        }

        // ── NPC 인벤토리 동봉 — npc_inventory: { npc_id: [items] } (Python PromptPayload 정합).
        // 아이템 획득/소모가 즉시 반영되도록 매 prompt 마다 동적 전송. LLM 이 보유 아이템만 GiveItem.
        if (UNPCInventoryComponent* InvComp = TargetNPC->GetInventoryComponent())
        {
            const FString InvJson = InvComp->GetInventoryJson();  // "[{id,name,...}, ...]"
            TArray<TSharedPtr<FJsonValue>> InvArr;
            const TSharedRef<TJsonReader<>> InvReader = TJsonReaderFactory<>::Create(InvJson);
            if (FJsonSerializer::Deserialize(InvReader, InvArr))
            {
                const TSharedRef<FJsonObject> InvObj = MakeShared<FJsonObject>();
                InvObj->SetArrayField(TargetNpcId, InvArr);
                Payload->SetObjectField(TEXT("npc_inventory"), InvObj);
            }
        }
    }

    // ── 유효 타깃 vocabulary — valid_targets: [키워드/AgentID/가구ID] (Python PromptPayload 정합).
    // Python 이 Stage1 구조화 스키마의 target enum 으로 강제 주입. ResolveActionTarget 이
    // 해석 가능한 키워드(Player/Self/Enemy/<AgentID>/<FurnitureID>)와 정확히 일치시켜, LLM 이
    // "Strategic Position" 류 해석 불가 자유문자열 target 을 내는 것을 원천 차단.
    if (NPCMap)
    {
        TArray<TSharedPtr<FJsonValue>> TargetsArr;
        TargetsArr.Add(MakeShared<FJsonValueString>(TEXT("Player")));
        TargetsArr.Add(MakeShared<FJsonValueString>(TEXT("Self")));
        TargetsArr.Add(MakeShared<FJsonValueString>(TEXT("Enemy")));
        for (const TPair<FString, ASmartNPC*>& Pair : NPCMap->GetActiveNPCs())
        {
            TargetsArr.Add(MakeShared<FJsonValueString>(Pair.Key));
        }

        // ── 가구 인지 컨텍스트 — 대상 NPC 반경 내 가구만 LLM 에 노출(공간 현실성).
        //   valid_targets: 빈 가구만 합류 — 점유·원거리 가구 지정을 enum 차원에서 원천 차단.
        //   nearby_furniture: 점유 포함 전부 — "자리가 없네요" 류 대사 근거.
        // 무타겟 Sit/Sleep 은 C++(ExecuteLifestyleAction)가 무동작 방어 — 여기 노출이 유일한 착석 경로.
        if (UFurnitureManager* FurnMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFurnitureManager>() : nullptr)
        {
            if (ASmartNPC* TargetNPC = GetNPCById(TargetNpcId))
            {
                const FVector NpcLoc = TargetNPC->GetActorLocation();
                TArray<TSharedPtr<FJsonValue>> FurnitureArr;

                for (const TPair<FString, AFurnitureActor*>& Pair : FurnMgr->GetActiveFurniture())
                {
                    AFurnitureActor* Furniture = Pair.Value;
                    if (!IsValid(Furniture)) continue;

                    const float Dist = FVector::Dist2D(Furniture->GetActorLocation(), NpcLoc);
                    if (Dist > FurnitureContextRange) continue;

                    const bool bOccupied = Furniture->IsOccupied();
                    if (!bOccupied)
                    {
                        TargetsArr.Add(MakeShared<FJsonValueString>(Pair.Key));
                    }

                    // enum 접두("EFurnitureType::") 없는 짧은 타입명 — 프롬프트 가독성.
                    FString TypeStr;
                    switch (Furniture->FurnitureType)
                    {
                        case EFurnitureType::Seat:     TypeStr = TEXT("Seat"); break;
                        case EFurnitureType::Bed:      TypeStr = TEXT("Bed"); break;
                        default:                       TypeStr = TEXT("Unknown"); break;
                    }

                    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                    Item->SetStringField(TEXT("id"), Pair.Key);
                    Item->SetStringField(TEXT("type"), TypeStr);
                    Item->SetBoolField(TEXT("occupied"), bOccupied);
                    Item->SetNumberField(TEXT("dist_m"), FMath::RoundToFloat(Dist) / 100.f); // cm → m, 소수 2자리 내
                    FurnitureArr.Add(MakeShared<FJsonValueObject>(Item));
                }

                if (FurnitureArr.Num() > 0)
                {
                    Payload->SetArrayField(TEXT("nearby_furniture"), FurnitureArr);
                }
            }
        }

        Payload->SetArrayField(TEXT("valid_targets"), TargetsArr);
    }

    Payload->SetBoolField(TEXT("requires_replan"), bRequiresReplan);

    FString PayloadStr;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
    FJsonSerializer::Serialize(Payload, Writer);

    const FString Envelope = FEnvelopeBuilder::BuildPrompt(PayloadStr);
    SendEnvelopePromptToLLM(Envelope);

    // 응답까지 수 초가 걸린다. 그동안 아무 표시가 없으면 플레이어는 말이 씹힌 줄 안다.
    if (ASmartNPC* TargetNPC = GetNPCById(TargetNpcId))
    {
        TargetNPC->ShowThinking();
    }

    UE_LOG(LogTemp, Log, TEXT("[NPCManager] 플레이어 발화 전송 — %s → %s: \"%s\""), *PlayerID, *TargetNpcId, *Text);
}

void UNPCManager::SendStateToMCP(const FGameStateData& StateData)
{
    if (LLMClient)
    {
        LLMClient->SendStateUpdate(StateData);
    }
}

void UNPCManager::OnLLMMessageReceived(const FString& JsonMessage)
{
    if (!NPCMap)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Received LLM response but NPCMap is not initialized."));
        return;
    }

    // 한 번만 deserialize → 모든 핸들러가 같은 FJsonObject를 공유.
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonMessage);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[NPCManager] Malformed LLM JSON. Raw (first 200): %.200s"), *JsonMessage);
        return;
    }

    // 어떤 타입이든 그 NPC 앞으로 온 응답이면 대기 표시를 끝낸다. 대사가 있는 응답은
    // ShowSubtitle 이 알아서 덮지만, 액션만 있는 응답은 여기서 풀지 않으면 워치독까지 점이 남는다.
    {
        FString RespondingAgent;
        if (!Root->TryGetStringField(TEXT("agent_id"), RespondingAgent))
        {
            const TSharedPtr<FJsonObject>* PayloadObj = nullptr;
            if (Root->TryGetObjectField(TEXT("payload"), PayloadObj) && PayloadObj)
            {
                (*PayloadObj)->TryGetStringField(TEXT("agent_id"), RespondingAgent);
            }
        }
        if (!RespondingAgent.IsEmpty())
        {
            if (ASmartNPC* NPC = NPCMap->GetValidNPC(RespondingAgent))
            {
                NPC->StopThinking();
            }
        }
    }

    // state_update 응답 — relations(호감도) 데이터가 포함된 경우 AffinityCache 갱신
    {
        FString AgentID;
        TMap<FString, int32> Relations;
        if (UMCPJsonUtils::ParseAffinityUpdateFromObject(Root, AgentID, Relations) && Relations.Num() > 0)
        {
            PendingStateUpdateAgents.Remove(AgentID);

            if (ASmartNPC* NPC = NPCMap->GetValidNPC(AgentID))
            {
                if (UNPCStateComponent* StateComp = NPC->GetStateComponent())
                {
                    int32 ChangedCount = 0;
                    for (const auto& Pair : Relations)
                    {
                        if (StateComp->AffinityCache.FindRef(Pair.Key) != Pair.Value)
                        {
                            StateComp->UpdateAffinity(Pair.Key, Pair.Value);
                            ++ChangedCount;
                        }
                    }
                }
            }
            return;
        }
    }

    // debug_prompt — 브라우저 디버그 대시보드가 친 말. 마이크(ASR)와 완전히 같은 경로로
    // 태우기 위해 여기서 SendPlayerDialogue 를 호출한다. 서버가 대신 그래프를 돌리지 않는
    // 이유: NPC 인벤토리·valid_targets·주변 가구·plan 캐시는 전부 UE5 가 prompt 마다
    // 조립해 보내는 값이라, 서버가 흉내내면 실제와 다른 입력으로 검증하게 된다.
    {
        FString DebugNpcId, DebugPlayerId, DebugText;
        if (UMCPJsonUtils::ParseDebugPromptFromObject(Root, DebugNpcId, DebugPlayerId, DebugText))
        {
            // 이 콜백은 WebSocket broadcast 루프 안이다. 여기서 곧바로 Send 하면 소켓
            // 매니저의 리스너 배열을 순회 도중 건드리게 되므로 다음 틱으로 미룬다
            // (npc_audio_response 의 PlayFromUrl 지연과 같은 이유).
            if (UWorld* World = GetWorld())
            {
                TWeakObjectPtr<UNPCManager> WeakThis(this);
                World->GetTimerManager().SetTimerForNextTick(
                    [WeakThis, DebugNpcId, DebugPlayerId, DebugText]()
                    {
                        if (UNPCManager* Self = WeakThis.Get())
                        {
                            Self->SendPlayerDialogue(
                                DebugPlayerId.IsEmpty() ? TEXT("Debug_Player") : DebugPlayerId,
                                DebugNpcId, DebugText);
                        }
                    });
            }
            return;
        }
    }

    // npc_audio_response — TTS 오디오 전달. 액션 배치와 분리된 별도 메시지.
    {
        FString NpcId, WsUrl, DialogueText, Emotion;
        int32 SampleRate = 16000;
        int32 Channels = 1;
        if (UMCPJsonUtils::ParseNpcAudioResponseFromObject(
                Root, NpcId, WsUrl, SampleRate, Channels, DialogueText, Emotion))
        {
            if (ASmartNPC* NPC = NPCMap ? NPCMap->GetValidNPC(NpcId) : nullptr)
            {
                // 머리 위 자막 — ws_url 있으면 음성 싱크(Started→표시/Completed→숨김),
                // 없으면(TTS 실패) 즉시 표시 + 타이머 폴백.
                NPC->ShowSubtitle(DialogueText, /*bWaitForAudio=*/!WsUrl.IsEmpty());

                if (UNPCAudioStreamComponent* AudioComp = NPC->FindComponentByClass<UNPCAudioStreamComponent>())
                {
                    if (!WsUrl.IsEmpty())
                    {
                        // PlayFromUrl 을 LLM WebSocket OnMessage 콜백 안에서 직접 호출하면,
                        // 그 안의 새 TTS WebSocket Connect() 가 IWebSocketsManager 의 tick listener
                        // array 를 broadcast 도중 mutate → ensure ("Array has changed during ranged-for")
                        // 다음 게임 틱으로 지연해서 broadcast 루프가 안전하게 끝난 뒤 연결한다.
                        if (UWorld* World = GetWorld())
                        {
                            TWeakObjectPtr<UNPCAudioStreamComponent> WeakAudio(AudioComp);
                            FString LocalWsUrl = WsUrl;
                            int32 LocalSampleRate = SampleRate;
                            int32 LocalChannels = Channels;
                            World->GetTimerManager().SetTimerForNextTick(
                                [WeakAudio, LocalWsUrl, LocalSampleRate, LocalChannels]()
                                {
                                    if (UNPCAudioStreamComponent* Comp = WeakAudio.Get())
                                    {
                                        Comp->PlayFromUrl(LocalWsUrl, LocalSampleRate, LocalChannels);
                                    }
                                });
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning,
                            TEXT("[NPCManager] npc_audio_response 수신했으나 ws_url 비어 있음 (TTS 실패 fallback). npc=%s text=%.60s"),
                            *NpcId, *DialogueText);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[NPCManager] npc_audio_response 수신했으나 NPC '%s' 에 UNPCAudioStreamComponent 가 첨부되어 있지 않음"),
                        *NpcId);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[NPCManager] npc_audio_response 의 NPC '%s' 를 NPCMap 에서 찾을 수 없음"), *NpcId);
            }
            return;
        }
    }

    // location_decision_result 메시지는 전술 위치 파이프라인으로 별도 라우팅
    {
        FString AgentID, ChosenCandidateId, Reason;
        int32 RequestGen = 0;
        if (UMCPJsonUtils::ParseLocationDecisionResultFromObject(Root, AgentID, ChosenCandidateId, Reason, RequestGen))
        {
            NPCMap->DeliverLocationDecision(AgentID, ChosenCandidateId, Reason, static_cast<uint32>(RequestGen));
            return;
        }

        // type은 location_decision_result인데 payload 파싱 실패 → WaitingLLM 고착 방지
        FString TypeStr;
        if (Root->TryGetStringField(TEXT("type"), TypeStr) && TypeStr == TEXT("location_decision_result"))
        {
            FString AgentIDFallback;
            const TSharedPtr<FJsonObject>* PayloadObj;
            if (Root->TryGetObjectField(TEXT("payload"), PayloadObj))
                (*PayloadObj)->TryGetStringField(TEXT("agent_id"), AgentIDFallback);

            if (!AgentIDFallback.IsEmpty())
            {
                if (ASmartNPC* NPC = NPCMap->GetValidNPC(AgentIDFallback))
                    if (UNPCActionComponent* AC = NPC->GetActionComponent())
                        AC->AbortTacticalQuery();
            }
            return;
        }
    }

    // ── 계획 캐싱 회신 수신 (Multi-NPC Cached Planning) ──────────────────
    // NpcPlans(PascalCase) 가 있으면 재계획 응답 → 해당 NPC 의 CurrentPlan 저장.
    // PlanAchieved(PascalCase) 가 있으면 e4b 가 plan 달성 감지 → FlagPlanAchieved() → 다음 턴 재계획.
    // 키: PascalCase 최상위(NpcPlans/ActionBatches/PlanAchieved), plan 내부 snake_case.
    {
        const TSharedPtr<FJsonObject>* NpcPlansObj = nullptr;
        const bool bHasPlans = Root->TryGetObjectField(TEXT("NpcPlans"), NpcPlansObj);

        const TSharedPtr<FJsonObject>* PlanAchievedObj = nullptr;
        const bool bHasPlanAchieved = Root->TryGetObjectField(TEXT("PlanAchieved"), PlanAchievedObj);

        const TSharedPtr<FJsonObject>* BatchesObj = nullptr;
        if (Root->TryGetObjectField(TEXT("ActionBatches"), BatchesObj))
        {
            for (const auto& BatchPair : (*BatchesObj)->Values)
            {
                const FString& AgentID = BatchPair.Key;
                ASmartNPC* NPC = NPCMap->GetValidNPC(AgentID);
                if (!NPC) { continue; }
                UNPCStateComponent* StateComp = NPC->GetStateComponent();
                if (!StateComp) { continue; }

                const TSharedPtr<FJsonObject>* PlanObj = nullptr;
                if (bHasPlans && (*NpcPlansObj)->TryGetObjectField(AgentID, PlanObj))
                {
                    FNPCPlan Plan;
                    (*PlanObj)->TryGetStringField(TEXT("goal"), Plan.Goal);
                    const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
                    if ((*PlanObj)->TryGetArrayField(TEXT("steps"), StepsArr))
                    {
                        for (const TSharedPtr<FJsonValue>& StepVal : *StepsArr)
                        {
                            FString Step;
                            if (StepVal.IsValid() && StepVal->TryGetString(Step))
                            {
                                Plan.Steps.Add(Step);
                            }
                        }
                    }
                    (*PlanObj)->TryGetNumberField(TEXT("relation_snapshot"), Plan.RelationSnapshot);
                    StateComp->SetCurrentPlan(Plan);
                    UE_LOG(LogTemp, Log, TEXT("[NPCManager] Plan 저장: %s goal=\"%s\" steps=%d"),
                        *AgentID, *Plan.Goal, Plan.Steps.Num());
                }

                // e4b plan 달성 신호 — 다음 턴 강제 재계획(새 plan 생성).
                if (bHasPlanAchieved)
                {
                    bool bAchieved = false;
                    if ((*PlanAchievedObj)->TryGetBoolField(AgentID, bAchieved) && bAchieved)
                    {
                        StateComp->FlagPlanAchieved();
                        UE_LOG(LogTemp, Log, TEXT("[NPCManager] Plan 달성 감지: %s → 다음 턴 재계획"), *AgentID);
                    }
                }
            }
        }
    }

    NPCMap->DeliverParsedActionBatches(Root);
}

void UNPCManager::SendEventReport(const FString& AgentID, const FString& CombinedPayload)
{
    // 이미 NPCStateComponent에서 취합/배치/JSON화가 끝난 데이터를 받음
    // 여기서는 Envelope 래핑만 해서 즉시 발송
    FString Envelope = FEnvelopeBuilder::BuildEmergencyReport(CombinedPayload);
    
    if (LLMClient && LLMClient->IsConnected())
    {
        LLMClient->SendPrompt(Envelope);
        UE_LOG(LogTemp, Warning, TEXT("[NPCManager] Event Report Sent for Agent: %s"), *AgentID);
    }
}

void UNPCManager::HandleNPCDialogue(const FString& AgentID, const FString& DialogueText)
{
    OnNPCResponseReceived.Broadcast(AgentID, DialogueText);

    // 머리 위 말풍선 — 액션 dialogue 경로(폴백: 즉시 표시 + 타이머).
    // 같은 발화의 TTS(npc_audio_response)가 뒤따르면 ShowSubtitle 가 음성 싱크로 전환.
    if (NPCMap)
    {
        if (ASmartNPC* NPC = NPCMap->GetValidNPC(AgentID))
        {
            NPC->ShowSubtitle(DialogueText, /*bWaitForAudio=*/false);
        }
    }

    // 응답 가시화 — 화면 자막(위젯과 병행, 검증/디버그용)
#if !UE_BUILD_SHIPPING
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Cyan,
            FString::Printf(TEXT("%s: %s"), *AgentID, *DialogueText));
    }
#endif
}
