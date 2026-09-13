#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/WebSocketClient.h"
#include "NPC/Struct/NPCActionTypes.h"
#include "Core/Types/GameStateData.h"
#include "NPCManager.generated.h"

class ASmartNPC;
class FJsonObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNPCResponseReceived, const FString&, NPCName, const FString&, Message);

UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API UNPCManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "MCP|Chat")
    FOnNPCResponseReceived OnNPCResponseReceived;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** WorldContext(액터·컴포넌트 등)에서 UNPCManager 획득 — GameInstance subsystem 이중 조회 idiom 일원화. 없으면 nullptr. */
    static UNPCManager* Get(const UObject* WorldContext);

    // === NPC 등록/해제 ===
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void RegisterNPC(const FString& AgentID, ASmartNPC* NPC);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void UnregisterNPC(const FString& AgentID);

    /** AgentID 로 등록된 NPC 조회 (없으면 nullptr). LLM target_id 키워드 해석용. */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    ASmartNPC* GetNPCById(const FString& AgentID) const;

    /** 등록된 전체 NPC 순회용 (근처 아군 탐색 등). */
    const TMap<FString, ASmartNPC*>& GetActiveNPCs() const { return ActiveNPCs; }

    // === 취합된 긴급 인지 이벤트 전송 (단일 LLM 채널, Python이 SLM/LLM 자동 라우팅) ===
    void SendEventReport(const FString& AgentID, const TSharedRef<FJsonObject>& Payload);

    // === 디버그: ActionBatch JSON 직접 주입(서버 없이 배치 분배 검증) ===
    UFUNCTION(BlueprintCallable, Category = "MCP|Debug")
    void OnWebSocketMessageReceived(const FString& JsonMessage);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendEnvelopePromptToLLM(const FString& JsonData);

    /** 플레이어 발화를 대상 NPC로 전송 (단순 대화).
     *  PromptPayload(snake_case) 조립 → BuildPrompt → SendEnvelopePromptToLLM.
     *  응답은 기존 ActionBatch(Dialogue) 경로로 NPC가 처리. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Dialogue")
    void SendPlayerDialogue(const FString& PlayerID, const FString& TargetNpcId, const FString& Text);

    /** 가구 인지 반경(cm) — 대화 대상 NPC 기준 이 거리 내 가구만 LLM 컨텍스트(nearby_furniture)·
     *  타겟 enum(valid_targets, 빈 가구만)에 노출. 공간 현실성: 멀리 있는 가구는 모름. */
    static constexpr float FurnitureContextRange = 1500.f;

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    void SendStateToMCP(const FGameStateData& StateData);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    bool IsServerConnected() const;

    /** state_update 주기 (초). 모든 등록 NPC에 대해 이 주기로 Python에 상태 동기화. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI")
    float StateUpdateInterval = 3.0f;

    // === 동시 넉다운 카운트 (액티브 래그돌) ===
    // GameInstanceSubsystem 이라 PIE 세션마다 새로 생성·리셋 → 과거 static 전역의 세션 잔존·멀티월드 공유 문제 회피.

    /** 현재 카운트 < Max 면 ++ 후 true(넉다운 허용), 상한 도달이면 false(호출측 Flinch 폴백). */
    bool TryEnterKnockdown(int32 Max)
    {
        if (ActiveKnockdownCount >= Max) { return false; }
        ++ActiveKnockdownCount;
        return true;
    }

    /** 기상·사망·EndPlay 시 카운트 감소(0 하한). */
    void ExitKnockdown()
    {
        ActiveKnockdownCount = (ActiveKnockdownCount > 0) ? (ActiveKnockdownCount - 1) : 0;
    }

private:
    /** 동시 넉다운 수(트리거형이라 평소 0). */
    int32 ActiveKnockdownCount = 0;

    /** AgentID → NPC. 등록/해제는 RegisterNPC/UnregisterNPC 만. */
    UPROPERTY()
    TMap<FString, ASmartNPC*> ActiveNPCs;

    UPROPERTY()
    ULLMNetworkClient* LLMClient;

    /** ActionBatch 를 해당 NPC 로 전달. 미등록 AgentID 는 경고 후 스킵. */
    void DeliverToNPC(const FString& TargetAgentID, const FActionBatch& ActionBatch);

    /** LLM이 선택한 전술 위치 후보 ID를 해당 NPC의 ActionComponent로 전달.
     *  RequestGen 은 EQS 요청 세대 번호 — 0 이면 stale 검사 우회(레거시 호환). */
    void DeliverLocationDecision(const FString& AgentID, const FString& ChosenCandidateId, const FString& Reason, uint32 RequestGen);

    /** 이미 파싱된 JSON에서 ModeActionRequest를 추출하여 NPC들에 분배. */
    void DeliverParsedActionBatches(const TSharedPtr<FJsonObject>& Root);

    FTimerHandle StateUpdateTimerHandle;

    /** state_update 응답 대기 중인 AgentID 집합 — 응답 전 재전송 차단용 */
    TSet<FString> PendingStateUpdateAgents;

    /** 모든 등록된 NPC에 대해 state_update를 Python으로 전송 (Python 응답에 relations 포함 → AffinityCache 갱신). */
    void TickStateUpdate();

    UFUNCTION()
    void OnLLMMessageReceived(const FString& JsonMessage);

    UFUNCTION()
    void HandleNPCDialogue(const FString& AgentID, const FString& DialogueText);
};
