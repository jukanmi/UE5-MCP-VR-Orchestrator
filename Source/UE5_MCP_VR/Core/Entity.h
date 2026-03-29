#pragma once

#include "CharacterAttributes.h"
#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "Entity.generated.h"


UENUM(BlueprintType)
enum class EEntityType : uint8 { None, Player, NPC, Item, InteractableObject };

// ============================================================================
// [1] IItem - 아이템 전용 인터페이스
// ============================================================================
UINTERFACE(MinimalAPI, BlueprintType)
class UItem : public UInterface {
  GENERATED_BODY()
};

class UE5_MCP_VR_API IItem {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item")
  FString GetEntityID() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item")
  EEntityType GetEntityType() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item")
  FVector GetEntityLocation() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item")
  bool IsPickupable() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item")
  FString GetItemID() const;
};

// ============================================================================
// [2] ICharacterBase - 캐릭터 최상위 인터페이스
// ============================================================================
UINTERFACE(MinimalAPI, BlueprintType)
class UCharacterBase : public UInterface {
  GENERATED_BODY()
};

// [주의] I* 인터페이스 클래스에는 멤버 변수를 선언할 수 없습니다.
// StateTags, EntityID 등의 데이터는 구현 클래스(ASmartNPC, AVRPlayerCharacter
// 등)에서 관리하세요.
class UE5_MCP_VR_API ICharacterBase : public IGameplayTagAssetInterface {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Character")
  FString GetEntityID() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Character")
  EEntityType GetEntityType() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Character")
  FVector GetEntityLocation() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
            Category = "Character|Attributes")
  FCharacterAttributesBase GetAttributes() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
            Category = "Character|Tags")
  void SetStateTag(FGameplayTag Tag);

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
            Category = "Character|Attributes")
  bool IsHostileTo(const TScriptInterface<ICharacterBase> &Other) const;
};

// ============================================================================
// [3] INPC - NPC 계층 (ICharacterBase 확장)
// ============================================================================
UINTERFACE(MinimalAPI, BlueprintType)
class UNPC : public UCharacterBase {
  GENERATED_BODY()
};

class UE5_MCP_VR_API INPC : public ICharacterBase {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Character|NPC")
  FString GetAgentID() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Character|NPC")
  FNPCAttributes GetNPCAttributes() const;
};

// ============================================================================
// [4] IPlayer - 플레이어 계층 (ICharacterBase 확장)
// ============================================================================
UINTERFACE(MinimalAPI, BlueprintType)
class UPlayerBase : public UCharacterBase {
  GENERATED_BODY()
};

class UE5_MCP_VR_API IPlayerBase : public ICharacterBase {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
            Category = "Character|Player")
  FString GetPlayerName() const;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
            Category = "Character|Player")
  FPlayerAttributes GetPlayerAttributes() const;
};