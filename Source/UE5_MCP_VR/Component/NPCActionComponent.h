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

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "NPC|Action|Queue")
    bool bIsDialogueActive = false;

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

protected:
    virtual void BeginPlay() override;

    // --- Internal Helpers ---
    void UpdateActionState(const FGameAction& Action);
    void DispatchActions(const TArray<FGameAction>& Actions);

    // Movement Speed 변환 (EMoveType → float)
    float ParseMoveSpeed(const EMoveType& Type) const;

    // ============================================================================
    // [기본 함수 (Base Functions)] 래퍼함수 구현시 사용하는 유틸 함수
    // ============================================================================
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseMove(FVector TargetLocation, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseDialogue(const FString& DialogueText, const EFacialState Emotion);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseEmotion(const EFacialState Emotion);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseFaceRotate(FVector TargetLocation, float TurnSpeed = 5.f);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSitDown(AActor* TargetSeat);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSitUp();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseLieDown(AActor* TargetBed);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseLieUp();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseStopCurrentAction();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    TMap<FString, int32> BaseDetectEntityInRange(float Range, EEntityType EntityType);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BaseSendEventToActor(AActor* TargetActor, const FString& EventName);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BasePlayMontage(const FString& MontageName);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void BasePlaySound(const FString& SoundName);

private:
    // Cached references
    ASmartNPCAIController* GetOwnerAIController() const;
    FString GetOwnerAgentID() const;
public:
    // ============================================================================
    // [EAction 래퍼 함수 (Action Wrappers)]
    // ============================================================================

    // ----------------------------------------------------------------------------
    // [1] Common Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteIdle();

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteMove(FVector Location, AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteFollow(AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDialogue(const FString& DialogueText, const EFacialState Emotion);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteTurnTo(FVector Location, AActor* TargetActor); 

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteScan(FVector Location, AActor* TargetActor);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteUseItem(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteEquipAction(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteUnequipAction(const FString& ItemID);

    // ----------------------------------------------------------------------------
    // [2] Combat Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteAttackAction(AActor* TargetActor, EAttackType AttackType);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteBlock(AActor* TargetActor); // TODO: 타겟을 향해 BaseFaceRotate 이후 BaseDefend(true) 기반 로직

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDodgeAction(FVector Direction); // TODO: 인자로 받은 Direction 방향에 맞춰 기본 함수 BaseDodge 연계 다이내믹 래핑

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteFlee(FVector EscapeLocation); // TODO: BaseMoveToLocation(Run) 형태로 구현

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSignalAllies(const FString& HandSign); // TODO: BaseHandSignal 래핑

    // ----------------------------------------------------------------------------
    // [3] Social Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, int32 GiveAmount, const FString& GetItemID, int32 GetAmount); // TODO: ExecuteInteraction 활용 (Get 조건 검사)
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteGiveItem(AActor* TargetActor, const FString& ItemID, int32 Amount); // TODO: ExecuteInteraction 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteComfort(AActor* TargetActor); // TODO: 쓰다듬기 인터랙션, 일단은 빈 구현체 혹은 로그 처리
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteHandObject(const FString& ItemID); // TODO: 인벤토리 검사 후 BaseEquip 활용

    // ----------------------------------------------------------------------------
    // [4] Task Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecutePickUp(FVector Location); // TODO: Location 반경 내 아이템 오브젝트 탐색 후 ExecuteInteraction("PickUp"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDrop(const FString& ItemID); // TODO: ExecuteInteraction("Drop"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteCraft(const TArray<FString>& ItemIDs); // TODO: ExecuteInteraction("Craft"...) 형태 래핑
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteRepair(const FString& ItemID); // TODO: ExecuteInteraction("Repair"...) 활용 및 스탯 복구

    // ----------------------------------------------------------------------------
    // [5] Investigation Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteInvestigate(FVector Location); // TODO: BaseMoveToLocation 후 ExecuteScan 래핑
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteScout(FVector StartLocation, FVector EndLocation); // TODO: 두 장소를 왕복 순찰하도록 BaseMoveToLocation 연계

    // ----------------------------------------------------------------------------
    // [6] Lifestyle Behaviors
    // ----------------------------------------------------------------------------
    // TODO: DanceName, SingName은 추후 UEnum 대체 고려 (일단은 FString 파라미터 활용)
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSit(AActor* TargetEntity); // TODO: ExecuteInteraction("SitDown"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSleep(AActor* TargetEntity); // TODO: ExecuteInteraction("LieDown"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteClean(FVector Location, float Radius); // TODO: ExecuteInteraction("Clean"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteRead(AActor* TargetEntity); // TODO: ExecuteInteraction("Read"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecutePray(FVector Location); // TODO: ExecuteInteraction("Pray"...) 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDance(const FString& DanceName); // TODO: ExecuteInteraction("Dance"...) 우선 활용
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSing(const FString& SingName); // TODO: ExecuteInteraction("Sing"...) 우선 활용

};
