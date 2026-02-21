#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Utils/MCPJsonUtils.h" // FGameAction, FActionBatch
#include "../AI/NPCActionTypes.h" // Enums
#include "NPCActionComponent.generated.h"

class ASmartNPCAIController;
class UNPCInteractionDataAsset;
class UNPCStateComponent;
class UNPCInventoryComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UE5_MCP_VR_API UNPCActionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPCActionComponent();

    // --- Dependencies (외부 컴포넌트 참조) ---
    // Owner에서 자동 검색하므로 Blueprint에서 직접 설정할 필요 없음
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Action|Refs")
    UNPCStateComponent* StateComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC|Action|Refs")
    UNPCInventoryComponent* InventoryComponent;

    // --- Interaction Configuration ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Action|Interaction")
    UNPCInteractionDataAsset* InteractionData;

    // --- Action Queue State ---
    TQueue<FGameAction> ActionQueue;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsBusy = false;

    // --- Public API: Batch & Queue ---

    /**
     * ActionBatch 전체를 실행합니다.
     * 1. BehaviorMode / FacialState 업데이트
     * 2. Actions 배열을 Dispatch (Dialogue는 즉시, 나머지는 Queue)
     */
    UFUNCTION(BlueprintCallable, Category = "NPC|Action")
    void ExecuteActionBatch(const FActionBatch& Batch);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    void ProcessNextAction();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    void OnActionCompleted();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Queue")
    void StopAllActions();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action")
    void AbortCurrentAction();

    // --- Public API: Direct Execute ---

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteMoveToLocation(FVector TargetLocation, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteKeepDistance(AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk, float Distance = 300.f);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteWait(float Duration);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDialogue(const FString& DialogueText, const EFacialState& EmotionID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteFaceRotate(FVector TargetLocation, float TurnSpeed = 5.f);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecutePerformAttack(const FString& AttackType);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDefend(bool bStartDefend);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDodge();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteHandSignal(const FString& SignalName);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteEmote(const FString& EmoteName);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteEquip(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteUnequip(const FString& ItemID);

    // --- Interaction System (Central Entry Point) ---
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Interaction")
    void ExecuteInteraction(const FString& InteractionType, AActor* TargetActor, const FString& TargetID, const FString& ExtraParams);



protected:
    virtual void BeginPlay() override;

    // --- Internal Helpers ---
    void UpdateActionState(const FGameAction& Action);
    void DispatchActions(const TArray<FGameAction>& Actions);

    // Movement Speed 변환 (EMoveType → float)
    float ParseMoveSpeed(const EMoveType& Type) const;


    // --- Interaction Sub-Functions ---
    void PlayInteractionMontage(const FString& Key);

    void ExecuteSitDown(AActor* TargetSeat);
    void ExecuteSitUp();
    void ExecuteLieDown(AActor* TargetBed);
    void ExecuteLieUp();
    void ExecutePickUp(AActor* TargetItem);
    void ExecuteDropItem(const FString& ItemID);
    void ExecuteEat(const FString& ItemID);
    void ExecuteWear(const FString& ItemID);
    void ExecuteClean(AActor* TargetZone);
    void ExecuteRepair(AActor* TargetObject);
    void ExecuteRead(AActor* TargetBook);
    void ExecutePray();
    void ExecuteDance(const FString& Style);
    void ExecuteSing(const FString& SongName);

private:
    // Cached references
    ASmartNPCAIController* GetOwnerAIController() const;
    FString GetOwnerAgentID() const;
};
