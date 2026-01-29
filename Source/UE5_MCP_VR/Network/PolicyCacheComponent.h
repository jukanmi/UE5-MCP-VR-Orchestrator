#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BehaviorPolicy.h"
#include "PolicyCacheComponent.generated.h"

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UE5_MCP_VR_API UPolicyCacheComponent : public UActorComponent
{
    GENERATED_BODY()

public:    
    UPolicyCacheComponent();

protected:
    virtual void BeginPlay() override;

public:    
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Core Logic
    void HandlePolicyUpdate(const FString& JsonPayload);
    uint32 GenerateDecisionSeed(int32 DecisionIndex);

private:
    void CheckExpiration();
    void ResolvePolicyTarget(const FGuid& TargetGuid);
    void UpdateBlackboard();
    void IncrementDecisionIndex();

private:
    UPROPERTY(VisibleAnywhere, Category = "Policy")
    FBehaviorPolicy CurrentPolicy;

    UPROPERTY(VisibleAnywhere, Category = "Policy")
    bool bHasValidPolicy;

    UPROPERTY(VisibleAnywhere, Category = "Policy")
    int32 CurrentDecisionIndex;
};
