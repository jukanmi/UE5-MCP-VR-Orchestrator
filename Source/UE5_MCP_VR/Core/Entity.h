#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../NPC/Struct/NPCActionTypes.h" // EEntityType, FActionBatch
#include "Entity.generated.h"


// ============================================================================
// [1] IEntity - 루트 인터페이스
// [의도(Why)] 월드 내 모든 상호작용 가능한 개체가 공통으로 제공해야 할 최소한의 식별 계약을 정의합니다.
//             AI, UI, 네트워크 등 시스템이 개체 유형에 관계없이 ID/위치를 조회할 수 있습니다.
// ============================================================================

UENUM(BlueprintType)
enum class EEntityType : uint8
{
	None,
	Player,
	NPC,
	Item,
	InteractableObject
};

UINTERFACE(MinimalAPI, BlueprintType)
class UEntity : public UInterface { GENERATED_BODY() };

class UE5_MCP_VR_API IEntity
{
    GENERATED_BODY()

private:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity")
    FString EntityID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity")
    EEntityType EntityType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity")
    FVector EntityLocation;

public:
    /** 백엔드(Python/LLM) 통신 시 사용하는 고유 식별자를 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity")
    FString GetEntityID() const { return EntityID; };

    /** 해당 개체의 분류(Player, NPC, Item 등)를 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity")
    EEntityType GetEntityType() const { return EntityType; };

    /** AI가 거리 계산이나 상호작용 판단을 할 수 있도록 현재 월드 위치를 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity")
    FVector GetEntityLocation() const { return EntityLocation; };
};


// ============================================================================
// [2] IItemEntity - 아이템 계층 (IEntity 확장)
// [의도(Why)] 아이템 고유의 획득 가능 여부, 아이템 데이터 조회 등을 표준화합니다.
//             NPC/Player가 아이템을 인식할 때 구체적 클래스 없이도 획득 여부를 판단할 수 있습니다.
// ============================================================================

UINTERFACE(MinimalAPI, BlueprintType)
class UItemEntity : public UEntity { GENERATED_BODY() };

class UE5_MCP_VR_API IItemEntity : public IEntity
{
    GENERATED_BODY()

public:
    /** 해당 아이템이 현재 월드 상에서 줍기(PickUp) 가능한 상태인지 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Item")
    bool IsPickupable() const;

    /** 아이템의 고유 키(ItemID)를 반환합니다. ItemDataAsset 조회의 기준이 됩니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Item")
    FString GetItemID() const;
};


// ============================================================================
// [3] ICharacterEntity - 캐릭터 계층 (IEntity 확장)
// [의도(Why)] 살아있는 존재(Player, NPC)가 공통으로 가져야 할 생존 상태(체력, 생사)를 표준화합니다.
//             전투 시스템이 대상이 Player든 NPC든 동일한 인터페이스로 피해를 줄 수 있습니다.
// ============================================================================

UINTERFACE(MinimalAPI, BlueprintType)
class UCharacterEntity : public UEntity { GENERATED_BODY() };

class UE5_MCP_VR_API ICharacterEntity : public IEntity
{
    GENERATED_BODY()

public:
    /** 현재 체력(HP) 값을 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Character")
    float GetHealth() const;

    /** 생존 여부를 반환합니다. (Health > 0) */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Character")
    bool IsAlive() const;

    /** 대상과의 적대 관계 여부를 반환합니다. 전투 AI의 공격 대상 선정에 사용됩니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Character")
    bool IsHostileTo(const TScriptInterface<ICharacterEntity>& Other) const;
};


// ============================================================================
// [4] INPCEntity - NPC 계층 (ICharacterEntity 확장)
// [의도(Why)] AI 시스템과 통신하는 NPC만이 가져야 할 AgentID, 행동 배치 실행 등을 표준화합니다.
//             NPCManager가 구체적 클래스(ASmartNPC)에 의존하지 않고 인터페이스를 통해 NPC를 제어할 수 있습니다.
// ============================================================================

UINTERFACE(MinimalAPI, BlueprintType)
class UNPCEntity : public UCharacterEntity { GENERATED_BODY() };

class UE5_MCP_VR_API INPCEntity : public ICharacterEntity
{
    GENERATED_BODY()

public:
    /** Python 백엔드에서 할당한 AI 에이전트 식별자를 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|NPC")
    FString GetAgentID() const;

    /** LLM으로부터 수신된 ActionBatch를 수행합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|NPC")
    void ExecuteActionBatch(const FActionBatch& Batch);
};


// ============================================================================
// [5] IPlayerEntity - 플레이어 계층 (ICharacterEntity 확장)
// [의도(Why)] 인간 플레이어 고유의 UI, 입력 등을 추상화합니다.
//             AI가 플레이어와 NPC를 구분하여 다르게 반응해야 할 때 활용합니다.
// ============================================================================

UINTERFACE(MinimalAPI, BlueprintType)
class UPlayerEntity : public UCharacterEntity { GENERATED_BODY() };

class UE5_MCP_VR_API IPlayerEntity : public ICharacterEntity
{
    GENERATED_BODY()

public:
    /** 플레이어의 표시 이름(닉네임 등)을 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Player")
    FString GetPlayerName() const;
};
