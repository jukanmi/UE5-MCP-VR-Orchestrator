#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "../NPC/Struct/NPCActionTypes.h" // EEntityType, FActionBatch
#include "Entity.generated.h"
#include "CharacterAttributes.h"

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


UINTERFACE(MinimalAPI, BlueprintType)
class UCharacterEntity : public UEntity { GENERATED_BODY() };

class UE5_MCP_VR_API ICharacterEntity : public IEntity, public IGameplayTagAssetInterface
{
    GENERATED_BODY()

public:


    /** 공통 캐릭터 속성(기초 스탯, 자원, 전투, 이동 등)을 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Attributes")
    FCharacterAttributesBase GetAttributes() const;

    /** 상태 태그를 추가합니다 (에: 전투 상태, 기절 등). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Tags")
    void AddStateTag(FGameplayTag Tag);

    /** 상태 태그를 제거합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Tags")
    void RemoveStateTag(FGameplayTag Tag);

    /** 대상과의 적대 관계 여부를 반환합니다. 전투 AI의 공격 대상 선정에 사용됩니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Attributes")
    bool IsHostileTo(const TScriptInterface<ICharacterEntity>& Other) const;
};


// ============================================================================
// [4] INPCEntity - NPC 계층 (ICharacterEntity 확장)
// [의도(Why)] AI 시스템과 통신하는 NPC만이 가져야 할 AgentID, 행동 배치 실행 등을 표준화합니다.
//             NPCManager가 구체적 클래스(ASmartNPC)에 의존하지 않고 인터페이스를 통해 NPC를 제어합니다.
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

    /** NPC 전용 속성(BehavioralTraits 포함)을 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|NPC")
    FNPCAttributes GetNPCAttributes() const;
};


// ============================================================================
// [5] IPlayerEntity - 플레이어 계층 (ICharacterEntity 확장)
// [의도(Why)] 인간 플레이어 고유의 UI, 입력 등을 추상화합니다.
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

    /** 플레이어 전용 속성(BehavioralTraits 없음)을 반환합니다. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Entity|Player")
    FPlayerAttributes GetPlayerAttributes() const;
};