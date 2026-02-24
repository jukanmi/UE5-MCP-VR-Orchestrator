#pragma once

#include "CoreMinimal.h"
#include "../Core/InventoryComponent.h" // Base Class
#include "NPCInventoryComponent.generated.h"


/**
 * NPC 전용 인벤토리 컴포넌트.
 * - 범용 InventoryComponent를 상속받아 기본 인벤토리 기능을 사용.
 * - LLM 연동을 위한 Context 생성 기능(JSON 변환) 추가.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCInventoryComponent : public UInventoryComponent
{
    GENERATED_BODY()

public:
    UNPCInventoryComponent();

protected:
    // 초기화
    virtual void BeginPlay() override;

public:
    // --- NPC/LLM Specific API ---

    /**
     * 현재 인벤토리 상태를 LLM 컨텍스트용 JSON 문자열로 변환합니다.
     * 형태: "[{id:'Sword', count:1}, ...]"
     */
    UFUNCTION(BlueprintCallable, Category = "Inventory|LLM")
    FString GetInventoryJson() const;
};
