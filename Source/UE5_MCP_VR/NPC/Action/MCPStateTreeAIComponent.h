#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeAIComponent.h"
#include "MCPStateTreeAIComponent.generated.h"

class UStateTree;

/**
 * UStateTreeAIComponent의 StateTreeRef는 protected이고 공개 setter가 없어
 * 런타임에 SmartNPC.StateTreeAsset을 주입할 수 없음.
 * 이 얇은 서브클래스가 setter 하나만 노출.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UMCPStateTreeAIComponent : public UStateTreeAIComponent
{
    GENERATED_BODY()

public:
    /** 런타임에 StateTree 에셋을 주입. StartLogic() 호출 전에 부르세요. */
    void SetStateTreeAsset(UStateTree* InStateTree);
};
