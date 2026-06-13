#include "NPCManager.h"
#include "SmartNPC.h"
#include "NPCStateComponent.h"
#include "NPCAudioStreamComponent.h"
#include "../Network/MCPJsonUtils.h"
#include "../Network/EnvelopeBuilder.h"
#include "Action/NPCActionComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"


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

    // PromptPayload 조립 — payload 키는 snake_case (CLAUDE.md §1)
    const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("player_id"), PlayerID);
    Payload->SetStringField(TEXT("voice_transcript"), Text);
    Payload->SetStringField(TEXT("target_npc_id"), TargetNpcId);

    // ── 계획 캐싱 분기 (Multi-NPC Cached Planning) ────────────────────────
    // 대상 NPC 의 ShouldReplan 판정 → requires_replan 송신. 대화 경로엔 실시간 perception
    // danger 가 없으므로 0 전달(턴 상한/plan 유무/danger pending 플래그로 판정).
    // 재계획 불필요 시 보관 plan 을 current_plan(snake_case)으로 동봉 → e4b 단독 컨텍스트 주입.
    bool bRequiresReplan = true;
    if (ASmartNPC* TargetNPC = GetNPCById(TargetNpcId))
    {
        if (UNPCStateComponent* StateComp = TargetNPC->GetStateComponent())
        {
            bRequiresReplan = StateComp->ShouldReplan(0.0f);
            if (!bRequiresReplan)
            {
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
    }
    Payload->SetBoolField(TEXT("requires_replan"), bRequiresReplan);

    FString PayloadStr;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
    FJsonSerializer::Serialize(Payload, Writer);

    const FString Envelope = FEnvelopeBuilder::BuildPrompt(PayloadStr);
    SendEnvelopePromptToLLM(Envelope);

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

    // npc_audio_response — TTS 통합 계획서 §3. 액션 배치와 분리된 별도 메시지.
    {
        FString NpcId, WsUrl, DialogueText, Emotion;
        int32 SampleRate = 16000;
        int32 Channels = 1;
        if (UMCPJsonUtils::ParseNpcAudioResponseFromObject(
                Root, NpcId, WsUrl, SampleRate, Channels, DialogueText, Emotion))
        {
            if (ASmartNPC* NPC = NPCMap->GetValidNPC(NpcId))
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
    // NpcPlans(PascalCase) 가 있으면 재계획 응답 → 해당 NPC 의 CurrentPlan 저장(턴 리셋).
    // ActionBatches 에만 있고 NpcPlans 에 없는 NPC 는 e4b 단독(경량) 응답 → 턴 카운터 +1.
    // 키: PascalCase 최상위(NpcPlans/ActionBatches), plan 내부 snake_case(goal/steps/relation_snapshot) — §1.
    {
        const TSharedPtr<FJsonObject>* NpcPlansObj = nullptr;
        const bool bHasPlans = Root->TryGetObjectField(TEXT("NpcPlans"), NpcPlansObj);

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
                else
                {
                    // 경량 루프 응답 — 턴 누적 (ReplanTurnLimit 도달 시 다음 prompt 강제 재계획).
                    StateComp->IncrementReplanTurn();
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
