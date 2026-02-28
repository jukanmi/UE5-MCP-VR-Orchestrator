// File: GameStateData.h
// Purpose: Python 백엔드로 전송할 UE5 월드 상태(State) 데이터를 담는 구조체 모음.
//          모든 필드는 LLM이 상황을 파악할 수 있는 '최소한의 컨텍스트'만 유지합니다.

#pragma once

#include "CoreMinimal.h"
#include "../NPC/Struct/NPCActionTypes.h"
#include "GameStateData.generated.h"

/**
 * FEntityState: 월드에 존재하는 단일 개체(NPC, 플레이어, 아이템)의 관찰 정보.
 * Python이 "근처에 누가, 어떤 상태로 있는지" 알 수 있어야 합니다.
 */
USTRUCT(BlueprintType)
struct FEntityState
{
    GENERATED_BODY()

    // 개체 고유 식별자 (ex: NPC_Elara, Player, Enemy_01)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    FString EntityID;

    // 개체 유형 (Player, NPC, Item, InteractableObject 등)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    EEntityType EntityType = EEntityType::None;

    // 이 NPC를 관찰하는 시점의 관찰자(NPC)로부터의 거리 (cm 단위)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    float Distance = 0.f;

    // 위협 여부 (적대적 존재 여부를 빠르게 알리기 위한 플래그)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    bool bIsHostile = false;
};

/**
 * FEQSResult: Environment Query System 공간 연산 결과값.
 * 무거운 3D 연산 결과물 중 '최상위 1~3개의 좌표'만 Python으로 전달합니다.
 */
USTRUCT(BlueprintType)
struct FEQSResult
{
    GENERATED_BODY()

    // 쿼리 목적 (예: "safe_position", "cover_location", "best_fire_position")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    FString QueryTag;

    // EQS 연산이 산출한 월드 공간 좌표 (cm 단위)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    FVector BestLocation = FVector::ZeroVector;

    // 해당 위치의 점수 (높을수록 좋은 위치)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    float Score = 0.f;
};

/**
 * FGameStateData: UE5 → Python 단방향 전송을 위한 상태 스냅샷(Snapshot).
 * NPC가 5초마다 한 번씩 자신의 상황을 정합화하여 채워보냅니다.
 */
USTRUCT(BlueprintType)
struct FGameStateData
{
    GENERATED_BODY()

    // 이 스냅샷을 생성한 NPC의 AgentID
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    FString OwnerAgentID;

    // 현재 NPC가 가진 행동 모드 상태
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    ENPCBehaviorMode CurrentMode = ENPCBehaviorMode::Common;

    // 현재 NPC의 월드 위치
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    FVector OwnerLocation = FVector::ZeroVector;

    // 인식 반경 내에 감지된 근처 개체 목록 (거리 기반 필터링됨)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    TArray<FEntityState> NearbyEntities;

    // 최근 EQS 쿼리로부터 받은 최상위 공간 연산 결과 (최대 3개)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    TArray<FEQSResult> EQSResults;

    // 현재 위협 수준 (None / Low / Medium / High)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    FString ThreatLevel = TEXT("None");

    // NPC가 현재 엄폐(Cover) 중인지 여부
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    bool bIsInCover = false;

    // 현재 타겟을 시야에 두고 있는지 여부
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    bool bHasLineOfSight = false;
};
