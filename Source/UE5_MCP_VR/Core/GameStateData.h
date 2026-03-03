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
 * 시각적 또는 청각적으로 감지된 단일 대상 데이터
 */
USTRUCT(BlueprintType)
struct FPerceptionData
{
    GENERATED_BODY()

    // 인지된 대상의 고유 ID 혹은 정체 불명 식별자 
    // (예: 너무 멀어 움직임만 보이거나, 소리만 들려 누군지 모를 경우 "unknown" 할당)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception")
    FString TargetID;

    // 감지된 센서 유형 (예: "Sight", "Hearing")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception")
    FString SenseType;
    
    // 대상의 월드 좌표 (cm 단위)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception")
    FVector Location = FVector::ZeroVector;

    // 대상까지의 거리
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception")
    float Distance = 0.f;

    // 시각적 감지용: 시야 내에 완벽하게 들어와 있는지 여부
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Perception")
    bool bInLineOfSight = false;
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

    // UE5 Perception 시스템에서 감지된 시각/청각 대상 목록
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|State")
    TArray<FPerceptionData> PerceivedTargets;

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
