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

    // 파라미터 문자열("(X=100.0,Y=200.0,Z=0.0)")을 FVector로 파싱하는 내부 구조체용 헬퍼 함수
    FVector ParseVectorParam(const FString& ParamStr) const;

    // 라우팅 래퍼: EAction에 따라 세부 Execute 함수들을 호출합니다. 파라미터를 Map 단위로 주고받아 확장성을 확보합니다.
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteInteraction(EAction ActionType, AActor* TargetActor, const TMap<FString, FString>& Params);

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
    void ExecuteBlock(AActor* TargetActor);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDodgeAction(FVector Direction);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteFlee(FVector EscapeLocation);

    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSignalAllies(const FString& HandSign);

    // ----------------------------------------------------------------------------
    // [3] Social Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteTrade(AActor* TargetActor, const FString& GiveItemID, int32 GiveAmount, const FString& GetItemID, int32 GetAmount);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteGiveItem(AActor* TargetActor, const FString& ItemID, int32 Amount);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteComfort(AActor* TargetActor);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteHandObject(const FString& ItemID);

    // ----------------------------------------------------------------------------
    // [4] Task Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecutePickUp(FVector Location);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDrop(const FString& ItemID);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteCraft(const TArray<FString>& ItemIDs);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteRepair(const FString& ItemID);

    // ----------------------------------------------------------------------------
    // [5] Investigation Behaviors
    // ----------------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteInvestigate(FVector Location);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteScout(FVector StartLocation, FVector EndLocation);

    // ----------------------------------------------------------------------------
    // [6] Lifestyle Behaviors
    // ----------------------------------------------------------------------------
    // TODO: DanceName, SingName은 추후 UEnum 대체 고려 (일단은 FString 파라미터 활용)
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSit(AActor* TargetEntity);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSleep(AActor* TargetEntity);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteClean(FVector Location, float Radius);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteRead(AActor* TargetEntity);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecutePray(FVector Location);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteDance(const FString& DanceName);
    
    UFUNCTION(BlueprintCallable, Category = "NPC|Action|Execute")
    void ExecuteSing(const FString& SingName);

};
