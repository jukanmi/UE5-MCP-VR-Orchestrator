#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "StorySubsystem.generated.h"

class AQuestMarkerActor;

/**
 * Python 스토리 디렉터가 응답 JSON 최상위 `Story` 블록으로 보내는 현재 퀘스트 상태.
 * {beat_id, quest_log, side: [...], events: [...]} — 비트 전이 직후 응답에만 실린다.
 */
USTRUCT(BlueprintType)
struct FStoryState
{
    GENERATED_BODY()

    /** 현재 메인 비트 id. "end" = 메인 퀘스트 완료. */
    UPROPERTY(BlueprintReadOnly, Category = "MCP|Story")
    FString BeatId;

    /** 플레이어 퀘스트 로그 한 문장(한국어). */
    UPROPERTY(BlueprintReadOnly, Category = "MCP|Story")
    FString QuestLog;

    /** 지금 드러난(active) 서브퀘스트 id 목록. */
    UPROPERTY(BlueprintReadOnly, Category = "MCP|Story")
    TArray<FString> Side;

    /** 해금됐지만 아직 수락 전(available) 서브퀘스트 id 목록 — 퀘스트 giver 머리 위 "!" 판정용. */
    UPROPERTY(BlueprintReadOnly, Category = "MCP|Story")
    TArray<FString> AvailableSide;

    /** 퀘스트 목표물 식별자(main.yaml quest_target_tag) — SmartNPC AgentID 또는 액터 Tag. 빈 문자열 = 마커 없음. */
    UPROPERTY(BlueprintReadOnly, Category = "MCP|Story")
    FString QuestTargetTag;

};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStoryUpdated, const FStoryState&, State);

/**
 * 스토리 상태 보관소 (GameInstanceSubsystem — NPCManager 패턴).
 *
 * NPCManager 가 서버 응답에서 `Story` 오브젝트를 발견하면 ApplyStoryJson 으로 넘긴다.
 * 여기서는 파싱·보관·브로드캐스트만 — 퀘스트 로그 WBP 는 OnStoryUpdated 에 바인딩하고,
 * 늦게 생성된 위젯은 GetCurrentState 로 마지막 값을 읽는다.
 * Phase C(`events` → spawn_enemy 실행)는 이 서브시스템에 ExecuteEvent 로 붙인다 — 지금은 파싱 안 함.
 */
UCLASS()
class UE5_MCP_VR_API UStorySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** 월드 컨텍스트에서 서브시스템 획득 — UNPCManager::Get 과 동일 패턴. */
    static UStorySubsystem* Get(const UObject* WorldContext);

    /** 응답 JSON 의 `Story` 오브젝트 → CurrentState 갱신 + OnStoryUpdated. 필수 키 없으면 false, 로그 후 무시. */
    bool ApplyStoryJson(const TSharedPtr<FJsonObject>& StoryObj);

    /** 개별 이벤트(spawn_enemy 등) 실행 */
    void ExecuteEvent(const TSharedPtr<FJsonObject>& EventObj);

    UFUNCTION(BlueprintPure, Category = "MCP|Story")
    const FStoryState& GetCurrentState() const { return CurrentState; }

    /** 서버 응답으로 한 번이라도 갱신됐는지 — 위젯이 빈 상태와 "아직 안 옴"을 구분할 때. */
    UFUNCTION(BlueprintPure, Category = "MCP|Story")
    bool HasState() const { return bHasState; }

    UPROPERTY(BlueprintAssignable, Category = "MCP|Story")
    FOnStoryUpdated OnStoryUpdated;

    /** 월드 마커(AQuestMarkerActor) 대상 지정 — AgentID(NPCManager) 우선, 없으면 액터 Tag. 빈 문자열·미발견 = 숨김.
     *  ApplyStoryJson 이 부르지만 디버그로 직접 호출해도 된다. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Story")
    void SetQuestTarget(const FString& Tag);

private:
    UPROPERTY()
    FStoryState CurrentState;

    UPROPERTY()
    TObjectPtr<AQuestMarkerActor> QuestMarker;

    bool bHasState = false;
};
