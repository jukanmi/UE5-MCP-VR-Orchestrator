#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PartySubsystem.generated.h"

/** 멤버가 바뀔 때마다 한 건씩 발행. bJoined=true 면 합류, false 면 해산. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPartyChanged, const FString&, AgentID, bool, bJoined);

/**
 * 플레이어 일행(파티) 멤버십의 단일 소유자 (GameInstanceSubsystem — NPCManager 와 같은 층).
 *
 * 누가 일행인지만 안다(AgentID 목록). 추적·일상 억제 같은 행동은 이 서브시스템 밖의 일이고,
 * 서버 통보(party_update)도 OnPartyChanged 를 듣는 쪽(NPCManager)이 맡는다 — 서버가 없어도 파티는 동작한다.
 */
UCLASS()
class UE5_MCP_VR_API UPartySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** 월드 컨텍스트에서 서브시스템 획득 — UNPCManager::Get 과 동일 패턴. 없으면 nullptr. */
    static UPartySubsystem* Get(const UObject* WorldContext);

    /** 최대 인원. 아군 SmartNPC 5명 중 4명까지. */
    static constexpr int32 MaxSize = 4;

    /** 합류. 이미 멤버면 변경 없이 true, 정원이 찼거나 빈 ID 면 false. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Party")
    bool Join(const FString& AgentID);

    /** 해산. 멤버가 아니면 무시. */
    UFUNCTION(BlueprintCallable, Category = "MCP|Party")
    void Leave(const FString& AgentID);

    UFUNCTION(BlueprintPure, Category = "MCP|Party")
    bool IsMember(const FString& AgentID) const { return Members.Contains(AgentID); }

    const TArray<FString>& GetMembers() const { return Members; }

    UPROPERTY(BlueprintAssignable, Category = "MCP|Party")
    FOnPartyChanged OnPartyChanged;

private:
    /** 합류 순서 유지. 최대 4개라 선형 탐색으로 충분. */
    TArray<FString> Members;
};
