#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BehaviorTree/BehaviorTree.h"
#include "../Utils/MCPJsonUtils.h" // For FGameAction struct
#include "NPCActionTypes.h" // For AI Enums and Structs
#include "CharacterAttributes.h"
#include "SmartNPC.generated.h"

class UNPCInteractionDataAsset;
class UNPCStateComponent;
class UNPCActionComponent;
class UNPCInventoryComponent;

/**
 * SmartNPC: Lightweight Facade & Identity Container.
 * 
 * [설계 원칙]
 * - SmartNPC는 NPC의 "정체성(Identity)"과 "중재자(Mediator)" 역할만 수행합니다.
 * - 실제 로직은 아래 3개의 컴포넌트에 위임됩니다:
 *   1. UNPCStateComponent   → 스탯, 표정, 데미지, 반사 판정
 *   2. UNPCActionComponent  → 행동 큐, Execute* 함수들, 상호작용
 *   3. UNPCInventoryComponent → 인벤토리, 장비
 *   내부적으로 컴포넌트에 위임하는 Facade 패턴입니다.
 */
UCLASS(BlueprintType, Blueprintable)
class UE5_MCP_VR_API ASmartNPC : public ACharacter
{
    GENERATED_BODY()

public:
    ASmartNPC();

    // === Components ===

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UNPCStateComponent* StateComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UNPCActionComponent* ActionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MCP|Components")
    UNPCInventoryComponent* InventoryComponent;

    // === Identity ===

    /** NPC 고유 ID (예: "Guard_1", "Merchant_A") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    FString AgentID;

    /** 이 NPC가 사용할 Behavior Tree */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|Identity")
    UBehaviorTree* BehaviorTreeAsset;

    // === Vision & Hearing Config (AIController에 적용됨) ===

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightRadius = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float LoseSightRadius = 3500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Vision")
    float SightAngle = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCP|AI|Hearing")
    float HearingRange = 3000.0f;

    // === Lifecycle ===

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // === Facade API (컴포넌트로 위임) ===

    /** 액션 배치 실행 → ActionComponent에 위임 */
    UFUNCTION(BlueprintCallable, Category = "MCP|AI")
    virtual void ExecuteActionBatch(const struct FActionBatch& Batch);

    /** 물리 상태 초기화 (애니메이션 중지, 이동 정지) */
    virtual void ClearPhysicalState();

    // === Damage Hook (UE5 Actor Override) ===

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        class AController* EventInstigator, AActor* DamageCauser) override;

    // === Convenience Accessors (자주 호출되는 것들의 Shortcut) ===

    /** Stats에 직접 접근하기 위한 편의 함수 */
    FCharacterAttributes& GetStats() const;



    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Queue")
    void OnActionCompleted();

    // === Facade: Execute* Wrappers ===

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteMoveToLocation(FVector TargetLocation, EMoveType SpeedType = EMoveType::Walk, float AcceptanceRadius = 50.f);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteKeepDistance(AActor* TargetActor, EMoveType SpeedType = EMoveType::Walk, float Distance = 300.f);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDialogue(const FString& DialogueText, const FString& EmotionID);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteWait(float Duration);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteFaceRotate(FVector TargetLocation, float TurnSpeed = 5.f);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void StopAllActions();

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecutePerformAttack(AActor* TargetActor, const FString& AttackType);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDefend(bool bStartDefend);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteDodge();

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteEquip(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteUnequip(const FString& ItemID);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteInteraction(const FString& InteractionType, AActor* TargetActor, const FString& TargetID, const FString& ExtraParams);

    void ExecuteKeepDistance(AActor* TargetActor, float Distance) { ExecuteKeepDistance(TargetActor, EMoveType::Walk, Distance); }

    void ExecuteKeepDistance(AActor* TargetActor, float Distance, float Speed);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteEmote(const FString& EmoteName);

    UFUNCTION(BlueprintCallable, Category = "MCP|AI|Action")
    void ExecuteHandSignal(const FString& SignalName);

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Social_Dialogue();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Common_Move();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Combat_Attack();

    UFUNCTION(CallInEditor, Category = "MCP|Debug")
    void Debug_Test_Orchestra_Pipeline();

    UFUNCTION(CallInEditor, Category = "MCP|Debug|Interaction")
    void Debug_Test_Interaction(FString InteractionKey = "Dance", FString ExtraParams = "Salsa");
};
