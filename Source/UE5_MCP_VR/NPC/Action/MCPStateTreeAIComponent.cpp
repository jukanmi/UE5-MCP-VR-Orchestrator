#include "MCPStateTreeAIComponent.h"
#include "StateTree.h"
#include "StateTreeReference.h"

void UMCPStateTreeAIComponent::SetStateTreeAsset(UStateTree* InStateTree)
{
    StateTreeRef.SetStateTree(InStateTree);
}
